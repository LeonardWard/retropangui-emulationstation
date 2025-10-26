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
							path = Utils::FileSystem::resolveRelativePath(path, romPath, false);
							gamelistPaths.insert(path);
						}
					}
				}
			}
		}

		for(auto it = mChildren.cbegin(); it != mChildren.cend(); it++)
		{
			FileData* child = *it;

			// Apply regular filter first
			if (idx->isFiltered() && !idx->showFile(child)) {
				continue;
			}

			// SCRAPED mode: show only games that exactly match gamelist.xml paths
			if (showFoldersSetting == "SCRAPED") {
				if (child->getType() == GAME) {
					if (gamelistPaths.find(child->getPath()) != gamelistPaths.end()) {
						if (!idx->isFiltered() || idx->showFile(child)) {
							mFilteredChildren.push_back(child);
						}
					}
				}
				// Skip folders in SCRAPED mode
				continue;
			}

			// AUTO mode: smart handling
			if (showFoldersSetting == "AUTO") {
				if (child->getType() == FOLDER) {
					std::vector<FileData*> folderChildren = child->getChildren();

					// Check if folder contains games from gamelist.xml
					FileData* gamelistGameInFolder = nullptr;
					for (auto grandchild : folderChildren) {
						if (grandchild->getType() == GAME &&
						    gamelistPaths.find(grandchild->getPath()) != gamelistPaths.end()) {
							gamelistGameInFolder = grandchild;
							break; // Found one, that's enough
						}
					}

					// If folder has a game from gamelist, show only that game
					if (gamelistGameInFolder != nullptr) {
						if (!idx->isFiltered() || idx->showFile(gamelistGameInFolder)) {
							mFilteredChildren.push_back(gamelistGameInFolder);
						}
						continue;
					}

					// No gamelist games - apply smart logic (.m3u priority)
					FileData* m3uFile = nullptr;
					std::vector<FileData*> playableFiles;

					for (auto grandchild : folderChildren) {
						if (grandchild->getType() != GAME) continue;

						std::string extension = Utils::FileSystem::getExtension(grandchild->getPath());
						std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

						if (extension == ".m3u") {
							m3uFile = grandchild;
						} else if (extension == ".cue" || extension == ".chd" ||
						           extension == ".iso" || extension == ".pbp") {
							playableFiles.push_back(grandchild);
						}
					}

					// If .m3u exists, show only .m3u
					if (m3uFile != nullptr) {
						if (!idx->isFiltered() || idx->showFile(m3uFile)) {
							mFilteredChildren.push_back(m3uFile);
						}
						continue;
					}

					// No .m3u - if 1 playable file, show it; otherwise show folder
					if (playableFiles.size() == 1) {
						if (!idx->isFiltered() || idx->showFile(playableFiles[0])) {
							mFilteredChildren.push_back(playableFiles[0]);
						}
						continue;
					}

					// Multiple files or no playable files - show folder
				}
				// AUTO mode: for GAME (non-folder) items
				else if (child->getType() == GAME) {
					// If in gamelist, show it; otherwise skip
					if (gamelistPaths.find(child->getPath()) != gamelistPaths.end()) {
						if (!idx->isFiltered() || idx->showFile(child)) {
							mFilteredChildren.push_back(child);
						}
						continue;
					}
					// Not in gamelist - skip this file
					continue;
				}
			}

			// If we reach here, add the item normally
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

	// RetroPangui: If cores are defined and command is empty, build command from settings
	if (!mEnvData->mCores.empty() && command.empty())
	{
		const CoreInfo& defaultCore = mEnvData->mCores[0]; // Already sorted by priority
		std::string systemName = mSystem->getName();

		std::string retroarchPath = Settings::getInstance()->getString("RetroArchPath");
		std::string coresPath = Settings::getInstance()->getString("LibretroCoresPath");
		std::string configPath = Settings::getInstance()->getString("CoreConfigPath");

		command = retroarchPath + " -L " + coresPath + "/" + defaultCore.name + "_libretro.so " +
		          "--config " + configPath + "/" + systemName + "/retroarch.cfg %ROM%";

		LOG(LogInfo) << "Using core: " << defaultCore.name << " (priority: " << defaultCore.priority << ")";
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
