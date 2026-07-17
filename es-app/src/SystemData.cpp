#include "SystemData.h"

#include "utils/FileSystemUtil.h"
#include "CollectionSystemManager.h"
#include "FileFilterIndex.h"
#include "FileSorts.h"
#include "Gamelist.h"
#include "Log.h"
#include "platform.h"
#include "Settings.h"
#include "ThemeData.h"
#include "views/UIModeController.h"
#include <fstream>
#include <random>
#include <set>
#include "utils/StringUtil.h"
#include "utils/ThreadPool.h"
#include "Window.h"

using namespace Utils;

std::vector<SystemData*> SystemData::sSystemVector;
std::vector<SystemData*> SystemData::sSystemVectorShuffled;
std::ranlux48 SystemData::sURNG = std::ranlux48(std::random_device()());

// RetroPangui: Disc image priority helper functions
// When multiple disc formats exist for the same game, only add the highest priority one.
// .ccd/.mds are intentionally NOT a priority tier here - they are never registered as a
// searchable <extension> in any es_systems.xml, so treating them as "higher priority" than
// .img/.bin/.iso just hid the raw image with nothing added in its place (2026-07-09 실기기에서 확인).
static int getDiscImagePriority(const std::string& extension)
{
	// Priority: 1 (highest) -> 2 (lowest)
	if (extension == ".cue" || extension == ".m3u") return 1;  // Cue sheets and playlists
	if (extension == ".img" || extension == ".iso" || extension == ".bin") return 2;  // Raw images
	return 0;  // Not a disc image
}

static bool hasHigherPriorityDiscImage(const std::string& filePath, const std::string& extension, const std::string& dirPath)
{
	int currentPriority = getDiscImagePriority(extension);
	if (currentPriority != 2) return false;  // Not a disc image, or already highest priority

	std::string stem = FileSystem::getStem(filePath);
	std::string basePath = dirPath + "/" + stem;

	if (FileSystem::exists(basePath + ".cue") || FileSystem::exists(basePath + ".CUE")) return true;
	if (FileSystem::exists(basePath + ".m3u") || FileSystem::exists(basePath + ".M3U")) return true;

	return false;
}

// RetroPangui: 이 폴더에 있는 .m3u 플레이리스트가 실제로 참조하는 파일들의 경로 집합을 반환.
// 위 hasHigherPriorityDiscImage()의 stem(파일명 동일) 매칭은 이 프로젝트가 실제로 쓰는
// 명명 관례(예: "Zeliard.m3u"가 "Zeliard (1987)(Game Arts)(Disk 1 of 4).zip"을 참조 - stem이
// 다름)에서 사실상 작동하지 않아, 개별 디스크 파일이 m3u와 별개로 계속 중복 등록되는 버그가
// 있었음(2026-07-15 실기기 보고). m3u 파일명이 아니라 그 안의 실제 내용을 신뢰하는 쪽이 이름
// 관례에 의존하지 않는 정확한 방법.
static std::set<std::string> getM3uMemberPaths(const std::string& folderPath)
{
	std::set<std::string> members;
	FileSystem::stringList dirContent = FileSystem::getDirContent(folderPath);
	for (FileSystem::stringList::const_iterator it = dirContent.cbegin(); it != dirContent.cend(); ++it)
	{
		std::string m3uExt = FileSystem::getExtension(*it);
		if (m3uExt != ".m3u" && m3uExt != ".M3U")
			continue;

		std::ifstream file(*it);
		if (!file.is_open())
			continue;

		std::string line;
		while (std::getline(file, line))
		{
			while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
				line.pop_back();
			size_t start = line.find_first_not_of(" \t");
			if (start == std::string::npos)
				continue;
			line = line.substr(start);
			if (line.empty() || line[0] == '#')
				continue;

			members.insert(FileSystem::resolveRelativePath(line, folderPath, false, true));
		}
	}
	return members;
}


SystemData::SystemData(const std::string& name, const std::string& fullName, SystemEnvironmentData* envData, const std::string& themeFolder, bool CollectionSystem) :
	mName(name), mFullName(fullName), mEnvData(envData), mThemeFolder(themeFolder), mIsCollectionSystem(CollectionSystem), mIsGameSystem(true)
{
	mFilterIndex = new FileFilterIndex();

	// if it's an actual system, initialize it, if not, just create the data structure
	if(!CollectionSystem)
	{
		mRootFolder = new FileData(FOLDER, mEnvData->mStartPath, mEnvData, this);
		mRootFolder->metadata.set("name", mFullName);

		if(!Settings::getInstance()->getBool("ParseGamelistOnly"))
			populateFolder(mRootFolder);

		if(!Settings::getInstance()->getBool("IgnoreGamelist"))
		{
			// gamelist.xml 없으면 현재 파일 목록으로 자동 생성
			if(!Utils::FileSystem::exists(mRootFolder->getPath() + "/gamelist.xml"))
				generateGamelist(this);
			parseGamelist(this);
		}

		mRootFolder->sort(FileSorts::SortTypes.at(0));

		indexAllGameFilters(mRootFolder);
	}
	else
	{
		// virtual systems are updated afterwards, we're just creating the data structure
		mRootFolder = new FileData(FOLDER, "" + name, mEnvData, this);
	}
	setIsGameSystemStatus();
	loadTheme();
}

SystemData::~SystemData()
{
	if(Settings::getInstance()->getString("SaveGamelistsMode") == "on exit")
		writeMetaData();

	delete mRootFolder;
	delete mFilterIndex;
}

void SystemData::setIsGameSystemStatus()
{
	// we exclude non-game systems from specific operations (i.e. the "RetroPie" system, at least)
	// if/when there are more in the future, maybe this can be a more complex method, with a proper list
	// but for now a simple string comparison is more performant
	mIsGameSystem = (mName != "retropie");
}

void SystemData::populateFolder(FileData* folder)
{
	const std::string& folderPath = folder->getPath();
	if(!Utils::FileSystem::isDirectory(folderPath))
	{
		LOG(LogWarning) << "Error - folder with path \"" << folderPath << "\" is not a directory!";
		return;
	}

	//make sure that this isn't a symlink to a thing we already have
	if(Utils::FileSystem::isSymlink(folderPath))
	{
		//if this symlink resolves to somewhere that's at the beginning of our path, it's gonna recurse
		if(folderPath.find(Utils::FileSystem::getCanonicalPath(folderPath)) == 0)
		{
			LOG(LogWarning) << "Skipping infinitely recursive symlink \"" << folderPath << "\"";
			return;
		}
	}

	std::string filePath;
	std::string extension;
	bool isGame;
	bool showHidden = Settings::getInstance()->getBool("ShowHiddenFiles");
	std::set<std::string> m3uMembers = getM3uMemberPaths(folderPath);
	Utils::FileSystem::stringList dirContent = Utils::FileSystem::getDirContent(folderPath);
	for(Utils::FileSystem::stringList::const_iterator it = dirContent.cbegin(); it != dirContent.cend(); ++it)
	{
		filePath = *it;

		// skip hidden files and folders
		if(!showHidden && Utils::FileSystem::isHidden(filePath))
			continue;

		//this is a little complicated because we allow a list of extensions to be defined (delimited with a space)
		//we first get the extension of the file itself:
		extension = Utils::FileSystem::getExtension(filePath);

		//fyi, folders *can* also match the extension and be added as games - this is mostly just to support higan
		//see issue #75: https://github.com/Aloshi/EmulationStation/issues/75

		isGame = false;
		if(std::find(mEnvData->mSearchExtensions.cbegin(), mEnvData->mSearchExtensions.cend(), extension) != mEnvData->mSearchExtensions.cend())
		{
			// RetroPangui: Skip if a higher priority disc image exists for the same game
			// e.g., if both .ccd and .img exist, only add .ccd
			// Also skip if this exact file is already referenced by an .m3u playlist in
			// this folder (content-based check, not filename-stem-based - see getM3uMemberPaths()).
			if (hasHigherPriorityDiscImage(filePath, extension, folderPath) || m3uMembers.count(filePath))
			{
				LOG(LogDebug) << "Skipping " << filePath << " - higher priority disc image exists or referenced by an .m3u playlist";
			}
			else
			{
				FileData* newGame = new FileData(GAME, filePath, mEnvData, this);

				// preventing new arcade assets to be added
				if(!newGame->isArcadeAsset())
				{
					folder->addChild(newGame);
					isGame = true;
				}
			}
		}

		//add directories that also do not match an extension as folders
		if(!isGame && Utils::FileSystem::isDirectory(filePath))
		{
			FileData* newFolder = new FileData(FOLDER, filePath, mEnvData, this);
			populateFolder(newFolder);

			//ignore folders that do not contain games
			if(newFolder->getChildrenByFilename().size() == 0)
				delete newFolder;
			else
				folder->addChild(newFolder);
		}
	}
}

// RetroPangui: refreshGamelist()용 경로 스캔.
// populateFolder()와 같은 규칙(확장자 매칭 - 디렉토리도 게임으로 인정(higan/.pc/.scummvm 폴더),
// 숨김 제외, 디스크 이미지 우선순위, 재귀 심볼릭 링크 가드)을 따르되 FileData를 만들지 않는다.
// FileData는 소멸 시 removeFromIndex()를 호출해 임시 스캔 용도로 쓰면 필터 인덱스
// 카운트가 오염되기 때문에 경로 문자열만 수집한다.
static void scanGamePaths(const std::string& folderPath, const std::vector<std::string>& extensions,
                          bool showHidden, std::vector<std::string>& out)
{
	if (!FileSystem::isDirectory(folderPath))
		return;

	if (FileSystem::isSymlink(folderPath) &&
	    folderPath.find(FileSystem::getCanonicalPath(folderPath)) == 0)
		return;

	std::set<std::string> m3uMembers = getM3uMemberPaths(folderPath);
	FileSystem::stringList dirContent = FileSystem::getDirContent(folderPath);
	for (FileSystem::stringList::const_iterator it = dirContent.cbegin(); it != dirContent.cend(); ++it)
	{
		const std::string& filePath = *it;

		if (!showHidden && FileSystem::isHidden(filePath))
			continue;

		std::string extension = FileSystem::getExtension(filePath);
		bool isGame = false;
		if (std::find(extensions.cbegin(), extensions.cend(), extension) != extensions.cend())
		{
			isGame = true;
			if (!hasHigherPriorityDiscImage(filePath, extension, folderPath) && !m3uMembers.count(filePath))
				out.push_back(filePath);
		}

		if (!isGame && FileSystem::isDirectory(filePath))
			scanGamePaths(filePath, extensions, showHidden, out);
	}
}

int SystemData::refreshGamelist(int* removedOut)
{
	if (removedOut)
		*removedOut = 0;

	if (mIsCollectionSystem || !mIsGameSystem)
		return 0;
	if (!FileSystem::isDirectory(mEnvData->mStartPath))
		return -1;

	std::vector<std::string> diskGames;
	scanGamePaths(mEnvData->mStartPath, mEnvData->mSearchExtensions,
	              Settings::getInstance()->getBool("ShowHiddenFiles"), diskGames);
	std::set<std::string> diskGameSet(diskGames.cbegin(), diskGames.cend());

	std::string xmlPath = getGamelistPath(true);

	// RetroPangui 2026-07-18(사용자 지시): 기존 <path> 노드를 찾아 지우고 다시
	// 넣는 diff-patch 방식 대신, 새 문서를 통째로 다시 써서 로직을 단순화.
	// 살아있는(디스크에 실제 존재하는) <game>은 노드를 그대로 복사해 메타데이터
	// 보존, 디스크에서 사라진 <game>은 복사하지 않고 개수만 셈(추가 감지만 하고
	// 삭제는 전혀 못 보던 기존 결함 해결). <folder> 등 다른 노드는 그대로 보존.
	// saveGamelistXml()이 교체 전 기존 파일을 .old로 백업해준다.
	pugi::xml_document oldDoc;
	pugi::xml_node oldRoot;
	if (FileSystem::exists(xmlPath))
	{
		pugi::xml_parse_result res = oldDoc.load_file(xmlPath.c_str());
		if (!res)
		{
			LOG(LogError) << "refreshGamelist: error parsing \"" << xmlPath << "\": " << res.description();
			return -1;
		}
		oldRoot = oldDoc.child("gameList");
	}

	pugi::xml_document newDoc;
	pugi::xml_node newRoot = newDoc.append_child("gameList");

	std::set<std::string> keptPaths;
	std::vector<std::string> removedPaths;
	if (oldRoot)
	{
		for (pugi::xml_node child = oldRoot.first_child(); child; child = child.next_sibling())
		{
			if (std::string(child.name()) == "game")
			{
				pugi::xml_node p = child.child("path");
				std::string absPath = p ? FileSystem::resolveRelativePath(
					p.text().get(), mEnvData->mStartPath, false, true) : "";

				// RetroPangui: 번들 게임(squashfs 직결, share에 물리 파일 없음)은
				// 디스크 스캔 대상이 아니므로 diskGameSet에 절대 없음 - 삭제로
				// 오인하지 않도록 별도로 보존. show/hide는 rpui-bundlegame.sh가
				// 노드 자체를 추가/제거하는 방식으로 처리(여기서 관여 안 함).
				if (!absPath.empty() &&
				    (diskGameSet.count(absPath) || isBundledRomPath(absPath, mName)))
				{
					newRoot.append_copy(child);
					keptPaths.insert(absPath);
				}
				else
				{
					removedPaths.push_back(absPath);
				}
			}
			else
			{
				newRoot.append_copy(child); // folder 등 - 이 함수가 다루는 대상이 아니라 그대로 보존
			}
		}
	}

	int added = 0;
	for (std::vector<std::string>::const_iterator it = diskGames.cbegin(); it != diskGames.cend(); ++it)
	{
		if (keptPaths.count(*it))
			continue;

		pugi::xml_node gameNode = newRoot.append_child("game");
		gameNode.append_child("path").text().set(
			FileSystem::createRelativePath(*it, mEnvData->mStartPath, false, true).c_str());
		gameNode.append_child("name").text().set(FileSystem::getStem(*it).c_str());
		added++;
	}

	int removed = (int)removedPaths.size();
	if (added == 0 && removed == 0)
		return 0;

	if (!saveGamelistXml(newDoc, xmlPath))
	{
		LOG(LogError) << "refreshGamelist: failed to write \"" << xmlPath << "\"";
		return -1;
	}

	// 사라진 게임을 메모리 트리에서도 제거 - 즐겨찾기/최근플레이 등 컬렉션에 이
	// 게임을 감싼 래퍼가 남아있으면 소스 삭제 후 댕글링 포인터가 되므로, "게임
	// 직접 삭제" 메뉴(GuiGamelistOptions)와 동일하게 먼저 컬렉션 쪽 래퍼부터
	// 정리한 뒤 소스 FileData를 지운다(소멸자가 부모/필터인덱스에서도 뗌).
	if (removed > 0)
	{
		std::vector<FileData*> allGames = mRootFolder->getFilesRecursive(GAME);
		for (std::vector<FileData*>::const_iterator it = allGames.cbegin(); it != allGames.cend(); ++it)
		{
			if (diskGameSet.count((*it)->getPath()))
				continue;
			CollectionSystemManager::get()->deleteCollectionFiles(*it);
			delete *it;
		}
	}

	// 새 항목을 메모리 트리에 반영(기존 항목은 findOrCreateFile이 그대로 반환)
	if (added > 0)
		parseGamelist(this);

	// 추가/삭제로 필터 인덱스가 어긋나지 않게 전체 재색인
	mFilterIndex->resetIndex();
	indexAllGameFilters(mRootFolder);

	LOG(LogInfo) << "refreshGamelist: \"" << mName << "\" +" << added << " -" << removed;
	if (removedOut)
		*removedOut = removed;
	return added;
}

void SystemData::indexAllGameFilters(const FileData* folder)
{
	const std::vector<FileData*>& children = folder->getChildren();

	for(std::vector<FileData*>::const_iterator it = children.cbegin(); it != children.cend(); ++it)
	{
		switch((*it)->getType())
		{
			case GAME:   { mFilterIndex->addToIndex(*it); } break;
			case FOLDER: { indexAllGameFilters(*it);      } break;
			default:
				LOG(LogInfo) << "Unknown type: " << (*it)->getType();
			     break;
		}
	}
}

std::vector<std::string> readList(const std::string& str, const char* delims = " \t\r\n,")
{
	std::vector<std::string> ret;

	size_t prevOff = str.find_first_not_of(delims, 0);
	size_t off = str.find_first_of(delims, prevOff);
	while(off != std::string::npos || prevOff != std::string::npos)
	{
		ret.push_back(str.substr(prevOff, off - prevOff));

		prevOff = str.find_first_not_of(delims, off);
		off = str.find_first_of(delims, prevOff);
	}

	return ret;
}


SystemData* SystemData::loadSystem(pugi::xml_node system)
{
	std::string name, fullname, path, cmd, themeFolder, defaultCore;

	name = system.child("name").text().get();
	fullname = system.child("fullname").text().get();
	path = system.child("path").text().get();
	defaultCore = system.child("defaultCore").text().get();

	std::vector<std::string> list = readList(system.child("extension").text().get());
	std::vector<std::string> extensions;

	for (auto extension = list.cbegin(); extension != list.cend(); extension++)
	{
		std::string xt = std::string(*extension);
		if (std::find(extensions.begin(), extensions.end(), xt) == extensions.end())
			extensions.push_back(xt);
	}

	cmd = system.child("command").text().get();

	// RetroPangui: Parse cores if available
	std::vector<CoreInfo> cores;
	pugi::xml_node coresNode = system.child("cores");
	if (coresNode)
	{
		for (pugi::xml_node coreNode = coresNode.child("core"); coreNode; coreNode = coreNode.next_sibling("core"))
		{
			CoreInfo coreInfo;
			coreInfo.name = coreNode.attribute("name").as_string();
			coreInfo.fullname = coreNode.attribute("fullname").as_string();
			// If fullname is not specified, use name
			if (coreInfo.fullname.empty())
				coreInfo.fullname = coreInfo.name;
			coreInfo.module_id = coreNode.attribute("module_id").as_string();
			coreInfo.priority = coreNode.attribute("priority").as_int(999);

			// Parse core-specific extensions
			std::string coreExtensions = coreNode.attribute("extensions").as_string();
			if (!coreExtensions.empty())
			{
				std::vector<std::string> coreExtList = readList(coreExtensions.c_str());
				for (auto& ext : coreExtList)
				{
					if (!ext.empty())
						coreInfo.extensions.push_back(ext);
				}
			}

			if (!coreInfo.name.empty())
			{
				cores.push_back(coreInfo);
				LOG(LogInfo) << "  Core: " << coreInfo.name << " (priority: " << coreInfo.priority << ")";
			}
		}

		// Sort cores by priority (lower number = higher priority)
		std::sort(cores.begin(), cores.end(), [](const CoreInfo& a, const CoreInfo& b) {
			return a.priority < b.priority;
		});
	}

	// platform id list
	const char* platformList = system.child("platform").text().get();
	std::vector<std::string> platformStrs = readList(platformList);
	std::vector<PlatformIds::PlatformId> platformIds;
	for (auto it = platformStrs.cbegin(); it != platformStrs.cend(); it++)
	{
		const char* str = it->c_str();
		PlatformIds::PlatformId platformId = PlatformIds::getPlatformId(str);

		if (platformId == PlatformIds::PLATFORM_IGNORE)
		{
			// when platform is ignore, do not allow other platforms
			platformIds.clear();
			platformIds.push_back(platformId);
			break;
		}

		// if there appears to be an actual platform ID supplied but it didn't match the list, warn
		if (str != NULL && str[0] != '\0' && platformId == PlatformIds::PLATFORM_UNKNOWN)
		{
			LOG(LogWarning) << "  Unknown platform for system \"" << name << "\" (platform \"" << str << "\" from list \"" << platformList << "\")";
		}
		else if (platformId != PlatformIds::PLATFORM_UNKNOWN)
			platformIds.push_back(platformId);
	}

	// theme folder
	themeFolder = system.child("theme").text().as_string(name.c_str());

	//validate
	// RetroPangui: Allow empty command if cores are defined
	if (name.empty() || path.empty() || extensions.empty() || (cmd.empty() && cores.empty()))
	{
		LOG(LogError) << "System \"" << name << "\" is missing name, path, extension, or command/cores!";
		return nullptr;
	}

	//convert path to generic directory seperators
	path = Utils::FileSystem::getGenericPath(path);

	//expand home symbol if the startpath contains ~
	if (path[0] == '~')
	{
		path.erase(0, 1);
		path.insert(0, Utils::FileSystem::getHomePath());
	}

	//create the system runtime environment data
	SystemEnvironmentData* envData = new SystemEnvironmentData;
	envData->mStartPath = path;
	envData->mSearchExtensions = extensions;
	envData->mLaunchCommand = cmd;
	envData->mPlatformIds = platformIds;
	envData->mCores = cores; // RetroPangui: Store cores

	SystemData* newSys = new SystemData(name, fullname, envData, themeFolder);
	if (newSys->getRootFolder()->getChildren().size() == 0)
	{
		LOG(LogWarning) << "System \"" << name << "\" has no games! Ignoring it.";
		delete newSys;

		return nullptr;
	}

	return newSys;
}

//creates systems from information located in a config file
bool SystemData::loadConfig(Window* window)
{
	deleteSystems();

	std::string path = getConfigPath(false);

	LOG(LogInfo) << "Loading system config file " << path << "...";

	if (!Utils::FileSystem::exists(path))
	{
		LOG(LogError) << "es_systems.xml file does not exist!";
		writeExampleConfig(getConfigPath(true));
		return false;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result res = doc.load_file(path.c_str());

	if(!res)
	{
		LOG(LogError) << "Could not parse es_systems.xml file!";
		LOG(LogError) << res.description();
		return false;
	}

	//actually read the file
	pugi::xml_node systemList = doc.child("systemList");

	if(!systemList)
	{
		LOG(LogError) << "es_systems.xml is missing the <systemList> tag!";
		return false;
	}

	std::vector<std::string> systemsNames;

	int systemCount = 0;
	for (pugi::xml_node system = systemList.child("system"); system; system = system.next_sibling("system"))
	{
		systemsNames.push_back(system.child("fullname").text().get());
		systemCount++;
	}

	int currentSystem = 0;

	typedef SystemData* SystemDataPtr;

	ThreadPool* pThreadPool = NULL;
	SystemDataPtr* systems = NULL;

	if (std::thread::hardware_concurrency() > 2 && Settings::getInstance()->getBool("ThreadedLoading"))
	{
		pThreadPool = new ThreadPool();

		systems = new SystemDataPtr[systemCount];
		for (int i = 0; i < systemCount; i++)
			systems[i] = nullptr;

		pThreadPool->queueWorkItem([] { CollectionSystemManager::get()->loadCollectionSystems(true); });
	}

	int processedSystem = 0;

	for (pugi::xml_node system = systemList.child("system"); system; system = system.next_sibling("system"))
	{
		if (pThreadPool != NULL)
		{
			pThreadPool->queueWorkItem([system, currentSystem, systems, &processedSystem]
			{
				systems[currentSystem] = loadSystem(system);
				processedSystem++;
			});
		}
		else
		{
			std::string fullname = system.child("fullname").text().get();

			if (window != NULL)
				window->renderLoadingScreen(fullname, systemCount == 0 ? 0 : (float)currentSystem / (float)(systemCount + 1));

			std::string nm = system.child("name").text().get();

			SystemData* pSystem = loadSystem(system);
			if (pSystem != nullptr)
				sSystemVector.push_back(pSystem);
		}

		currentSystem++;
	}

	if (pThreadPool != NULL)
	{
		if (window != NULL)
		{
			pThreadPool->wait([window, &processedSystem, systemCount, &systemsNames]
			{
				int px = processedSystem - 1;
				if (px >= 0 && px < systemsNames.size())
					window->renderLoadingScreen(systemsNames.at(px), (float)px / (float)(systemCount + 1));
			}, 10);
		}
		else
			pThreadPool->wait();

		for (int i = 0; i < systemCount; i++)
		{
			SystemData* pSystem = systems[i];
			if (pSystem != nullptr)
				sSystemVector.push_back(pSystem);
		}

		delete[] systems;
		delete pThreadPool;

		if (window != NULL)
			window->renderLoadingScreen("Favorites", systemCount == 0 ? 0 : currentSystem / systemCount);

		CollectionSystemManager::get()->updateSystemsList();
	}
	else
	{
		if (window != NULL)
			window->renderLoadingScreen("Favorites", systemCount == 0 ? 0 : currentSystem / systemCount);

		CollectionSystemManager::get()->loadCollectionSystems();
	}

	return true;
}

void SystemData::writeExampleConfig(const std::string& path)
{
	std::ofstream file(path.c_str());

	file << "<!-- This is the EmulationStation Systems configuration file.\n"
			"All systems must be contained within the <systemList> tag.-->\n"
			"\n"
			"<systemList>\n"
			"	<!-- Here's an example system to get you started. -->\n"
			"	<system>\n"
			"\n"
			"		<!-- A short name, used internally. Traditionally lower-case. -->\n"
			"		<name>nes</name>\n"
			"\n"
			"		<!-- A \"pretty\" name, displayed in menus and such. -->\n"
			"		<fullname>Nintendo Entertainment System</fullname>\n"
			"\n"
			"		<!-- The path to start searching for ROMs in. '~' will be expanded to $HOME on Linux or %HOMEPATH% on Windows. -->\n"
			"		<path>~/roms/nes</path>\n"
			"\n"
			"		<!-- A list of extensions to search for, delimited by any of the whitespace characters (\", \\r\\n\\t\").\n"
			"		You MUST include the period at the start of the extension! It's also case sensitive. -->\n"
			"		<extension>.nes .NES</extension>\n"
			"\n"
			"		<!-- The shell command executed when a game is selected. A few special tags are replaced if found in a command:\n"
			"		%ROM% is replaced by a bash-special-character-escaped absolute path to the ROM.\n"
			"		%BASENAME% is replaced by the \"base\" name of the ROM.  For example, \"/foo/bar.rom\" would have a basename of \"bar\". Useful for MAME.\n"
			"		%ROM_RAW% is the raw, unescaped path to the ROM. -->\n"
			"		<command>retroarch -L ~/cores/libretro-fceumm.so %ROM%</command>\n"
			"\n"
			"		<!-- The platform to use when scraping. You can see the full list of accepted platforms in src/PlatformIds.cpp.\n"
			"		It's case sensitive, but everything is lowercase. This tag is optional.\n"
			"		You can use multiple platforms too, delimited with any of the whitespace characters (\", \\r\\n\\t\"), eg: \"genesis, megadrive\" -->\n"
			"		<platform>nes</platform>\n"
			"\n"
			"		<!-- The theme to load from the current theme set.  See THEMES.md for more information.\n"
			"		This tag is optional. If not set, it will default to the value of <name>. -->\n"
			"		<theme>nes</theme>\n"
			"	</system>\n"
			"</systemList>\n";

	file.close();

	LOG(LogError) << "Example config written!  Go read it at \"" << path << "\"!";
}

void SystemData::deleteSystems()
{
	for(unsigned int i = 0; i < sSystemVector.size(); i++)
	{
		delete sSystemVector.at(i);
	}
	sSystemVector.clear();
}

std::string SystemData::getConfigPath(bool forWrite)
{
	std::string path = Utils::FileSystem::getHomePath() + "/.emulationstation/es_systems.xml";
	if(forWrite || Utils::FileSystem::exists(path))
		return path;

	return "/etc/emulationstation/es_systems.xml";
}

bool SystemData::isVisible()
{
   return (getDisplayedGameCount() > 0 ||
           (UIModeController::getInstance()->isUIModeFull() && mIsCollectionSystem) ||
           (mIsCollectionSystem && mName == "favorites"));
}

SystemData* SystemData::getNext() const
{
	std::vector<SystemData*>::const_iterator it = getIterator();

	do {
		it++;
		if (it == sSystemVector.cend())
			it = sSystemVector.cbegin();
	} while (!(*it)->isVisible());
	// as we are starting in a valid gamelistview, this will always succeed, even if we have to come full circle.

	return *it;
}

SystemData* SystemData::getPrev() const
{
	std::vector<SystemData*>::const_reverse_iterator it = getRevIterator();

	do {
		it++;
		if (it == sSystemVector.crend())
			it = sSystemVector.crbegin();
	} while (!(*it)->isVisible());
	// as we are starting in a valid gamelistview, this will always succeed, even if we have to come full circle.

	return *it;
}

std::string SystemData::getGamelistPath(bool forWrite) const
{
	std::string filePath;

	// ROM 폴더 gamelist.xml 우선: 있으면 읽기/쓰기 모두, forWrite면 없어도 여기에 생성
	filePath = mRootFolder->getPath() + "/gamelist.xml";
	if(Utils::FileSystem::exists(filePath) || forWrite)
		return filePath;

	// 읽기 전용 fallback: 홈 디렉토리
	filePath = Utils::FileSystem::getHomePath() + "/.emulationstation/gamelists/" + mName + "/gamelist.xml";
	if(Utils::FileSystem::exists(filePath))
		return filePath;

	return "/etc/emulationstation/gamelists/" + mName + "/gamelist.xml";
}

std::string SystemData::getThemePath() const
{
	// where we check for themes, in order:
	// 1. [SYSTEM_PATH]/theme.xml
	// 2. system theme from currently selected theme set [CURRENT_THEME_PATH]/[SYSTEM]/theme.xml
	// 3. default system theme from currently selected theme set [CURRENT_THEME_PATH]/theme.xml

	// first, check game folder
	std::string localThemePath = mRootFolder->getPath() + "/theme.xml";
	if(Utils::FileSystem::exists(localThemePath))
		return localThemePath;

	// not in game folder, try system theme in theme sets
	localThemePath = ThemeData::getThemeFromCurrentSet(mThemeFolder);

	if (Utils::FileSystem::exists(localThemePath))
		return localThemePath;

	// not system theme, try default system theme in theme set
	localThemePath = Utils::FileSystem::getParent(Utils::FileSystem::getParent(localThemePath)) + "/theme.xml";

	return localThemePath;
}

bool SystemData::hasGamelist() const
{
	return (Utils::FileSystem::exists(getGamelistPath(false)));
}

unsigned int SystemData::getGameCount() const
{
	return (unsigned int)mRootFolder->getFilesRecursive(GAME).size();
}

SystemData* SystemData::getRandomSystem()
{
	if (sSystemVector.empty()) return NULL;

	if (sSystemVectorShuffled.empty())
	{
		std::copy_if(sSystemVector.begin(), sSystemVector.end(), std::back_inserter(sSystemVectorShuffled), [](SystemData *sd){ return sd->isGameSystem(); });
		if (sSystemVectorShuffled.empty()) return NULL;

		std::shuffle(sSystemVectorShuffled.begin(), sSystemVectorShuffled.end(), sURNG);
	}

	SystemData* random_system = sSystemVectorShuffled.back();
	sSystemVectorShuffled.pop_back();
	return random_system;
}

void SystemData::setShuffledCacheDirty()
{
	mGamesShuffled.clear();
}

FileData* SystemData::getRandomGame()
{
	if (mGamesShuffled.empty())
	{
		mGamesShuffled = mRootFolder->getFilesRecursive(GAME, true);
		if (mGamesShuffled.empty()) return NULL;
		std::shuffle(mGamesShuffled.begin(), mGamesShuffled.end(), sURNG);
	}
	FileData* random_game = mGamesShuffled.back();
	mGamesShuffled.pop_back();
	return random_game;
}

unsigned int SystemData::getDisplayedGameCount() const
{
	// RetroPangui: Use getChildrenListToDisplay which properly handles ShowFolders setting
	const std::vector<FileData*>& displayedChildren = mRootFolder->getChildrenListToDisplay();

	// Count only GAME type (not folders)
	unsigned int count = 0;
	for (FileData* child : displayedChildren)
	{
		if (child->getType() == GAME)
			count++;
	}

	// Also recursively count games in folders
	std::function<void(FileData*)> countRecursive = [&](FileData* folder) {
		const std::vector<FileData*>& children = folder->getChildrenListToDisplay();
		for (FileData* child : children)
		{
			if (child->getType() == GAME)
				count++;
			else if (child->getType() == FOLDER)
				countRecursive(child);
		}
	};

	// Count games in subfolders
	for (FileData* child : displayedChildren)
	{
		if (child->getType() == FOLDER)
			countRecursive(child);
	}

	return count;
}

// RetroPangui: Count favorited games among displayed games (mirrors getDisplayedGameCount)
unsigned int SystemData::getDisplayedFavoriteCount() const
{
	const std::vector<FileData*>& displayedChildren = mRootFolder->getChildrenListToDisplay();

	unsigned int count = 0;
	for (FileData* child : displayedChildren)
	{
		if (child->getType() == GAME && child->metadata.get("favorite") == "true")
			count++;
	}

	std::function<void(FileData*)> countRecursive = [&](FileData* folder) {
		const std::vector<FileData*>& children = folder->getChildrenListToDisplay();
		for (FileData* child : children)
		{
			if (child->getType() == GAME && child->metadata.get("favorite") == "true")
				count++;
			else if (child->getType() == FOLDER)
				countRecursive(child);
		}
	};

	for (FileData* child : displayedChildren)
	{
		if (child->getType() == FOLDER)
			countRecursive(child);
	}

	return count;
}

void SystemData::loadTheme()
{
	mTheme = std::make_shared<ThemeData>();

	std::string path = getThemePath();

	if(!Utils::FileSystem::exists(path)) // no theme available for this platform
		return;

	try
	{
		// build map with system variables for theme to use,
		std::map<std::string, std::string> sysData;
		sysData.insert(std::pair<std::string, std::string>("system.name", getName()));
		sysData.insert(std::pair<std::string, std::string>("system.theme", getThemeFolder()));
		sysData.insert(std::pair<std::string, std::string>("system.fullName", getFullName()));

		mTheme->loadFile(sysData, path);
	} catch(ThemeException& e)
	{
		LOG(LogError) << e.what();
		mTheme = std::make_shared<ThemeData>(); // reset to empty
	}
}

void SystemData::writeMetaData() {
	if(Settings::getInstance()->getBool("IgnoreGamelist") || mIsCollectionSystem)
		return;

	//save changed game data back to xml
	updateGamelist(this);
}

void SystemData::onMetaDataSavePoint() {
	if(Settings::getInstance()->getString("SaveGamelistsMode") != "always")
		return;

	writeMetaData();
}

// RetroPangui: Get available emulator cores sorted by priority
std::vector<CoreInfo> SystemData::getCores() const
{
	if (!mEnvData || mEnvData->mCores.empty())
		return std::vector<CoreInfo>();

	// Copy cores vector
	std::vector<CoreInfo> sortedCores = mEnvData->mCores;

	// Sort by priority (lower number = higher priority)
	std::sort(sortedCores.begin(), sortedCores.end(),
		[](const CoreInfo& a, const CoreInfo& b) {
			return a.priority < b.priority;
		});

	return sortedCores;
}
