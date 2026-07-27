#pragma once
#ifndef ES_CORE_UTILS_FILE_SYSTEM_UTIL_H
#define ES_CORE_UTILS_FILE_SYSTEM_UTIL_H

#include <list>
#include <string>

namespace Utils
{
	namespace FileSystem
	{
		typedef std::list<std::string> stringList;

		stringList  getDirContent      (const std::string& _path, const bool _recursive = false);
		stringList  getPathList        (const std::string& _path);
		void        setHomePath        (const std::string& _path);
		std::string getHomePath        ();
		std::string getCWDPath         ();
		void        setExePath         (const std::string& _path);
		std::string getExePath         ();
		std::string getPreferredPath   (const std::string& _path);
		std::string getGenericPath     (const std::string& _path);
		std::string getEscapedPath     (const std::string& _path);
		std::string getCanonicalPath   (const std::string& _path);
		std::string getAbsolutePath    (const std::string& _path, const std::string& _base = getCWDPath());
		std::string getParent          (const std::string& _path);
		std::string getFileName        (const std::string& _path);
		std::string getStem            (const std::string& _path);
		std::string getExtension       (const std::string& _path);
		std::string resolveRelativePath(const std::string& _path, const std::string& _relativeTo, const bool _allowHome, const bool _skipDirectoryCheck);
		std::string createRelativePath (const std::string& _path, const std::string& _relativeTo, const bool _allowHome, const bool _skipDirectoryCheck);
		std::string removeCommonPath   (const std::string& _path, const std::string& _common, bool& _contains, const bool _skipDirectoryCheck);
		std::string resolveSymlink     (const std::string& _path);
		bool        removeFile         (const std::string& _path);
		bool        createDirectory    (const std::string& _path);
		bool        exists             (const std::string& _path);
		// RetroPangui: exists()의 pathExistsIndex 캐시를 직접 갱신 - pugixml
		// save_file()/std::rename()처럼 이 파일의 다른 함수를 거치지 않고
		// 파일을 만든 뒤, 같은 프로세스 내에서 곧바로 exists()를 다시
		// 체크해야 하는 호출부(예: Gamelist.cpp의 saveGamelistXml)를 위함.
		void        updateExistsCache  (const std::string& _path, bool _exists);
		bool        isAbsolute         (const std::string& _path);
		bool        isRegularFile      (const std::string& _path);
		bool        isDirectory        (const std::string& _path);
		bool        isSymlink          (const std::string& _path);
		bool        isHidden           (const std::string& _path);
#if !defined(_WIN32)
		bool        isExecutable       (const std::string& _path);
#endif // !_WIN32

	} // FileSystem::

} // Utils::

#endif // ES_CORE_UTILS_FILE_SYSTEM_UTIL_H
