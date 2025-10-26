#include "FileData.h"

#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"
#include "utils/TimeUtil.h"
#include "AudioManager.h"
#include "CollectionSystemManager.h"
#include "FileFilterIndex.h"
#include "FileSorts.h"
#include "InputManager.h"
#include "Log.h"
#include "MameNames.h"
#include "platform.h"
#include "Scripting.h"
#include "Settings.h"
#include "SystemData.h"
#include "VolumeControl.h"
#include "Window.h"
#include <assert.h>
#include <set>
#include <algorithm>
#include <functional>
#include <pugixml.hpp>

FileData::FileData(FileType type, const std::string& path, SystemEnvironmentData* envData, SystemData* system)
	: mType(type), mPath(path), mSystem(system), mEnvData(envData), mSourceFileData(NULL), mParent(NULL), metadata(type == GAME ? GAME_METADATA : FOLDER_METADATA) // metadata is REALLY set in the constructor!
{
	// metadata needs at least a name field (since that's what getName() will return)
	if(metadata.get("name").empty())
		metadata.set("name", getDisplayName());
	mSystemName = system->getName();
	metadata.resetChangedFlag();
}

FileData::~FileData()
{
	if(mParent)
		mParent->removeChild(this);

	if(mType == GAME)
		mSystem->getIndex()->removeFromIndex(this);

	mChildren.clear();
}

std::string FileData::getDisplayName() const
{
	std::string stem = Utils::FileSystem::getStem(mPath);
	if(mSystem && mSystem->hasPlatformId(PlatformIds::ARCADE) || mSystem->hasPlatformId(PlatformIds::NEOGEO))
		stem = MameNames::getInstance()->getRealName(stem);

	return stem;
}

std::string FileData::getCleanName() const
{
	return Utils::String::removeParenthesis(this->getDisplayName());
}

const std::string FileData::getThumbnailPath() const
{
	std::string thumbnail = metadata.get("thumbnail");

	// no thumbnail, try image
	if(thumbnail.empty())
	{
		thumbnail = metadata.get("image");

		// no image, try to use local image
		if(thumbnail.empty() && Settings::getInstance()->getBool("LocalArt"))
		{
			const char* extList[2] = { ".png", ".jpg" };
			for(int i = 0; i < 2; i++)
			{
				if(thumbnail.empty())
				{
					std::string path = mEnvData->mStartPath + "/images/" + getDisplayName() + "-image" + extList[i];
					if(Utils::FileSystem::exists(path))
						thumbnail = path;
				}
			}
		}
	}

	return thumbnail;
}

const std::string& FileData::getName()
{
	// RetroPangui: In ALL mode, show filename/foldername as-is
	if (Settings::getInstance()->getString("ShowFolders") == "ALL") {
		// Return filename with extension for files, folder name for folders
		static std::string filename;
		filename = Utils::FileSystem::getFileName(mPath);
		return filename;
	}

	return metadata.get("name");
}

const std::string& FileData::getSortName()
{
	if (metadata.get("sortname").empty())
		return metadata.get("name");
	else
		return metadata.get("sortname");
}

const std::vector<FileData*>& FileData::getChildrenListToDisplay() {

	FileFilterIndex* idx = CollectionSystemManager::get()->getSystemToView(mSystem)->getIndex();
	std::string showFoldersSetting = Settings::getInstance()->getString("ShowFolders");

	// RetroPangui: gamelist.xml-based filtering
	bool needsFolderFiltering = (showFoldersSetting == "SCRAPED" || showFoldersSetting == "AUTO");

	if (idx->isFiltered() || needsFolderFiltering) {
		mFilteredChildren.clear();

		// Parse gamelist.xml directly to get registered game paths
		std::set<std::string> gamelistPaths;
		if (showFoldersSetting == "SCRAPED" || showFoldersSetting == "AUTO") {
			std::string gamelistPath = mSystem->getGamelistPath(false);
			if (Utils::FileSystem::exists(gamelistPath)) {
				pugi::xml_document doc;
				pugi::xml_parse_result result = doc.load_file(gamelistPath.c_str());
				if (result) {
					pugi::xml_node root = doc.child("gameList");
					std::string romPath = Utils::FileSystem::getParent(gamelistPath);

					for (pugi::xml_node gameNode = root.child("game"); gameNode; gameNode = gameNode.next_sibling("game")) {
						pugi::xml_node pathNode = gameNode.child("path");
						if (pathNode) {
							std::string path = pathNode.text().get();
							// Resolve relative path
							path = Utils::FileSystem::resolveRelativePath(path, romPath, false, true);
							gamelistPaths.insert(path);
						}
					}
				}
			}
		}

		// Helper: Find FileData by path (recursive search)
		auto findFileByPath = [&](const std::string& targetPath) -> FileData* {
			std::function<FileData*(FileData*, const std::string&)> search =
				[&search](FileData* node, const std::string& path) -> FileData* {
				if (node->getType() == GAME && node->getPath() == path) {
					return node;
				}
				for (auto child : node->getChildren()) {
					FileData* result = search(child, path);
					if (result != nullptr) return result;
				}
				return nullptr;
			};
			return search(const_cast<FileData*>(this), targetPath);
		};

		// Helper function to check if file should be shown (smart filtering)
		auto shouldShowFile = [&](FileData* file) -> bool {
			std::string ext = Utils::FileSystem::getExtension(file->getPath());
			std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

			// Priority extensions that should always show
			if (ext == ".m3u" || ext == ".chd" || ext == ".iso" || ext == ".pbp" ||
			    ext == ".cue" || ext == ".ccd" || ext == ".img") {
				return true;
			}

			// Skip .bin files if there's a corresponding .cue
			if (ext == ".bin") {
				std::string cuePath = Utils::FileSystem::getParent(file->getPath()) + "/" +
				                      Utils::FileSystem::getStem(file->getPath()) + ".cue";
				if (Utils::FileSystem::exists(cuePath)) {
					return false; // Skip .bin if .cue exists
				}
				return true; // Show .bin if no .cue
			}

			return true; // Show other files
		};

		// === SCRAPED MODE: Use gamelist.xml directly ===
		if (showFoldersSetting == "SCRAPED") {
			// Iterate through gamelist paths and find corresponding FileData
			for (const std::string& path : gamelistPaths) {
				FileData* file = findFileByPath(path);
				if (file != nullptr) {
					if (!idx->isFiltered() || idx->showFile(file)) {
						mFilteredChildren.push_back(file);
					}
				}
			}
			return mFilteredChildren;
		}

		// === AUTO MODE: Registered games first, then smart filtering ===
		if (showFoldersSetting == "AUTO") {
			std::set<std::string> addedPaths; // Track what we've already added

			// Step 1: Add registered games first (gamelist.xml priority)
			for (const std::string& path : gamelistPaths) {
				FileData* file = findFileByPath(path);
				if (file != nullptr) {
					if (!idx->isFiltered() || idx->showFile(file)) {
						mFilteredChildren.push_back(file);
						addedPaths.insert(path);
					}
				}
			}

			// Step 2: Add unregistered items with smart filtering
			for(auto it = mChildren.cbegin(); it != mChildren.cend(); it++)
			{
				FileData* child = *it;

				// Apply regular filter first
				if (idx->isFiltered() && !idx->showFile(child)) {
					continue;
				}

				if (child->getType() == FOLDER) {
					// Check if folder has any registered games
					std::vector<FileData*> folderGames = child->getFilesRecursive(GAME, false);
					bool hasRegisteredGame = false;
					for (auto game : folderGames) {
						if (addedPaths.find(game->getPath()) != addedPaths.end()) {
							hasRegisteredGame = true;
							break;
						}
					}

					if (hasRegisteredGame) {
						// Skip folder, registered games already added individually
						continue;
					}

					// Unregistered folder - apply smart logic
					FileData* m3uFile = nullptr;
					FileData* cueFile = nullptr;
					int playableCount = 0;

					for (auto game : folderGames) {
						std::string ext = Utils::FileSystem::getExtension(game->getPath());
						std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

						if (ext == ".m3u") {
							m3uFile = game;
						} else if (ext == ".cue") {
							cueFile = game;
							playableCount++;
						} else if (ext == ".chd" || ext == ".iso" || ext == ".pbp") {
							playableCount++;
						}
					}

					// If .m3u exists, show it directly (skip folder)
					if (m3uFile != nullptr) {
						if (!idx->isFiltered() || idx->showFile(m3uFile)) {
							mFilteredChildren.push_back(m3uFile);
						}
						continue;
					}

					// If exactly 1 playable file, show it directly
					if (playableCount == 1 && cueFile != nullptr) {
						if (!idx->isFiltered() || idx->showFile(cueFile)) {
							mFilteredChildren.push_back(cueFile);
						}
						continue;
					}

					// Multiple files or other cases - show folder
					mFilteredChildren.push_back(child);
					continue;
				}
				else if (child->getType() == GAME) {
					// Skip if already added as registered game
					if (addedPaths.find(child->getPath()) != addedPaths.end()) {
						continue;
					}

					// Unregistered file - apply smart filtering
					if (shouldShowFile(child)) {
						if (!idx->isFiltered() || idx->showFile(child)) {
							mFilteredChildren.push_back(child);
						}
					}
					continue;
				}
			}
			return mFilteredChildren;
		}

		// Other filtering modes
		for(auto it = mChildren.cbegin(); it != mChildren.cend(); it++)
		{
			FileData* child = *it;
			if (idx->isFiltered() && !idx->showFile(child)) {
				continue;
			}
			mFilteredChildren.push_back(child);
		}

		return mFilteredChildren;
	}
	else
	{
		// ALL mode or no filtering needed
		return mChildren;
	}
}


const std::string FileData::getVideoPath() const
{
	std::string video = metadata.get("video");

	// no video, try to use local video
	if(video.empty() && Settings::getInstance()->getBool("LocalArt"))
	{
		std::string path = mEnvData->mStartPath + "/images/" + getDisplayName() + "-video.mp4";
		if(Utils::FileSystem::exists(path))
			video = path;
	}

	return video;
}

const std::string FileData::getMarqueePath() const
{
	std::string marquee = metadata.get("marquee");

	// no marquee, try to use local marquee
	if(marquee.empty() && Settings::getInstance()->getBool("LocalArt"))
	{
		const char* extList[2] = { ".png", ".jpg" };
		for(int i = 0; i < 2; i++)
		{
			if(marquee.empty())
			{
				std::string path = mEnvData->mStartPath + "/images/" + getDisplayName() + "-marquee" + extList[i];
				if(Utils::FileSystem::exists(path))
					marquee = path;
			}
		}
	}

	return marquee;
}

const std::string FileData::getImagePath() const
{
	std::string image = metadata.get("image");

	// no image, try to use local image
	if(image.empty())
	{
		const char* extList[2] = { ".png", ".jpg" };
		for(int i = 0; i < 2; i++)
		{
			if(image.empty())
			{
				std::string path = mEnvData->mStartPath + "/images/" + getDisplayName() + "-image" + extList[i];
				if(Utils::FileSystem::exists(path))
					image = path;
			}
		}
	}

	return image;
}

std::vector<FileData*> FileData::getFilesRecursive(unsigned int typeMask, bool displayedOnly) const
{
	std::vector<FileData*> out;
	FileFilterIndex* idx = mSystem->getIndex();

	for(auto it = mChildren.cbegin(); it != mChildren.cend(); it++)
	{
		if((*it)->getType() & typeMask)
		{
			if (!displayedOnly || !idx->isFiltered() || idx->showFile(*it))
				out.push_back(*it);
		}

		if((*it)->getChildren().size() > 0)
		{
			std::vector<FileData*> subchildren = (*it)->getFilesRecursive(typeMask, displayedOnly);
			out.insert(out.cend(), subchildren.cbegin(), subchildren.cend());
		}
	}

	return out;
}

std::string FileData::getKey() {
	return getFileName();
}

const bool FileData::isArcadeAsset()
{
	const std::string stem = Utils::FileSystem::getStem(mPath);
	return (
		(mSystem && (mSystem->hasPlatformId(PlatformIds::ARCADE) || mSystem->hasPlatformId(PlatformIds::NEOGEO)))
		&&
		(MameNames::getInstance()->isBios(stem) || MameNames::getInstance()->isDevice(stem))
	);
}

FileData* FileData::getSourceFileData()
{
	return this;
}

void FileData::addChild(FileData* file)
{
	assert(mType == FOLDER);
	assert(file->getParent() == NULL);

	const std::string key = file->getKey();
	if (mChildrenByFilename.find(key) == mChildrenByFilename.cend())
	{
		mChildrenByFilename[key] = file;
		mChildren.push_back(file);
		file->mParent = this;
	}
}

void FileData::removeChild(FileData* file)
{
	assert(mType == FOLDER);
	assert(file->getParent() == this);
	mChildrenByFilename.erase(file->getKey());
	for(auto it = mChildren.cbegin(); it != mChildren.cend(); it++)
	{
		if(*it == file)
		{
			file->mParent = NULL;
			mChildren.erase(it);
			return;
		}
	}

	// File somehow wasn't in our children.
	assert(false);

}

void FileData::sort(ComparisonFunction& comparator, bool ascending)
{
	if (ascending)
	{
		std::stable_sort(mChildren.begin(), mChildren.end(), comparator);
		for(auto it = mChildren.cbegin(); it != mChildren.cend(); it++)
		{
			if((*it)->getChildren().size() > 0)
				(*it)->sort(comparator, ascending);
		}
	}
	else
	{
		std::stable_sort(mChildren.rbegin(), mChildren.rend(), comparator);
		for(auto it = mChildren.rbegin(); it != mChildren.rend(); it++)
		{
			if((*it)->getChildren().size() > 0)
				(*it)->sort(comparator, ascending);
		}
	}
}

void FileData::sort(const SortType& type)
{
	sort(*type.comparisonFunction, type.ascending);
	mSortDesc = type.description;
}

void FileData::launchGame(Window* window)
{
	LOG(LogInfo) << "Attempting to launch game...";

	AudioManager::getInstance()->deinit();
	VolumeControl::getInstance()->deinit();
	InputManager::getInstance()->deinit();
	window->deinit();

	std::string command = mEnvData->mLaunchCommand;

	// RetroPangui: Handle %CORE% and %CONFIG% variables
	if (command.find("%CORE%") != std::string::npos || command.find("%CONFIG%") != std::string::npos)
	{
		// Get the file extension
		std::string gameExt = Utils::FileSystem::getExtension(getPath());

		// Find the best matching core for this extension
		std::string selectedCore;
		for (const auto& core : mEnvData->mCores)
		{
			// Check if this core supports the game's extension
			for (const auto& ext : core.extensions)
			{
				if (ext == gameExt)
				{
					selectedCore = core.name;
					LOG(LogInfo) << "Using core: " << selectedCore << " (priority: " << core.priority << ")";
					break;
				}
			}
			if (!selectedCore.empty()) break;
		}

		// If no matching core found, use the first (highest priority) core
		if (selectedCore.empty() && !mEnvData->mCores.empty())
		{
			selectedCore = mEnvData->mCores[0].name;
			LOG(LogInfo) << "Using default core: " << selectedCore;
		}

		// Replace %CORE% with full core path
		if (!selectedCore.empty())
		{
			std::string corePath = "/opt/retropangui/libretro/cores/" + selectedCore + "_libretro.so";
			command = Utils::String::replace(command, "%CORE%", corePath);
		}

		// Replace %CONFIG% with system-specific config path
		std::string configPath = "/home/pangui/share/system/configs/cores/" + mSystem->getName() + "/retroarch.cfg";
		command = Utils::String::replace(command, "%CONFIG%", configPath);
	}

	const std::string rom      = Utils::FileSystem::getEscapedPath(getPath());
	const std::string basename = Utils::FileSystem::getStem(getPath());
	const std::string rom_raw  = Utils::FileSystem::getPreferredPath(getPath());
	const std::string name     = getName();

	command = Utils::String::replace(command, "%ROM%", rom);
	command = Utils::String::replace(command, "%BASENAME%", basename);
	command = Utils::String::replace(command, "%ROM_RAW%", rom_raw);

	Scripting::fireEvent("game-start", rom, basename, name);

	LOG(LogInfo) << "	" << command;
	int exitCode = runSystemCommand(command);

	if(exitCode != 0)
	{
		LOG(LogWarning) << "...launch terminated with nonzero exit code " << exitCode << "!";
	}

	Scripting::fireEvent("game-end");

	window->init();
	InputManager::getInstance()->init();
	VolumeControl::getInstance()->init();
	window->normalizeNextUpdate();

	//update number of times the game has been launched

	FileData* gameToUpdate = getSourceFileData();

	int timesPlayed = gameToUpdate->metadata.getInt("playcount") + 1;
	gameToUpdate->metadata.set("playcount", std::to_string(static_cast<long long>(timesPlayed)));

	//update last played time
	gameToUpdate->metadata.set("lastplayed", Utils::Time::DateTime(Utils::Time::now()));
	CollectionSystemManager::get()->refreshCollectionSystems(gameToUpdate);

	gameToUpdate->mSystem->onMetaDataSavePoint();
}

CollectionFileData::CollectionFileData(FileData* file, SystemData* system)
	: FileData(file->getSourceFileData()->getType(), file->getSourceFileData()->getPath(), file->getSourceFileData()->getSystemEnvData(), system)
{
	// we use this constructor to create a clone of the filedata, and change its system
	mSourceFileData = file->getSourceFileData();
	refreshMetadata();
	mParent = NULL;
	metadata = mSourceFileData->metadata;
	mSystemName = mSourceFileData->getSystem()->getName();
}

CollectionFileData::~CollectionFileData()
{
	// need to remove collection file data at the collection object destructor
	if(mParent)
		mParent->removeChild(this);
	mParent = NULL;
}

std::string CollectionFileData::getKey() {
	return getFullPath();
}

FileData* CollectionFileData::getSourceFileData()
{
	return mSourceFileData;
}

void CollectionFileData::refreshMetadata()
{
	metadata = mSourceFileData->metadata;
	mDirty = true;
}

const std::string& CollectionFileData::getName()
{
	if (mDirty) {
		mCollectionFileName = Utils::String::removeParenthesis(mSourceFileData->metadata.get("name"));
		mCollectionFileName += " [" + Utils::String::toUpper(mSourceFileData->getSystem()->getName()) + "]";
		mDirty = false;
	}

	if (Settings::getInstance()->getBool("CollectionShowSystemInfo"))
		return mCollectionFileName;
	return mSourceFileData->metadata.get("name");
}

// returns Sort Type based on a string description
FileData::SortType getSortTypeFromString(std::string desc) {
	std::vector<FileData::SortType> SortTypes = FileSorts::SortTypes;
	// find it
	for(unsigned int i = 0; i < FileSorts::SortTypes.size(); i++)
	{
		const FileData::SortType& sort = FileSorts::SortTypes.at(i);
		if(sort.description == desc)
		{
			return sort;
		}
	}
	// if not found default to "name, ascending"
	return FileSorts::SortTypes.at(0);
}
