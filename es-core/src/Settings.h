#pragma once
#ifndef ES_CORE_SETTINGS_H
#define ES_CORE_SETTINGS_H

#include <map>
#include <set>
#include <string>

//This is a singleton for storing settings.
class Settings
{
public:
	static const int ONE_MINUTE_IN_MS = 1000 * 60;
	static Settings* getInstance();

	void loadFile();
	void saveFile();
	void loadRetropanguiConf();	// retropangui.conf 의 emulationstation.* 키 적용 (Log::open() 이후 main에서 호출)

	//You will get a warning if you try a get on a key that is not already present.
	bool getBool(const std::string& name);
	int getInt(const std::string& name);
	float getFloat(const std::string& name);
	const std::string& getString(const std::string& name);
	const std::map<std::string, int> getMap(const std::string& name);

	void setBool(const std::string& name, bool value);
	void setInt(const std::string& name, int value);
	void setFloat(const std::string& name, float value);
	void setString(const std::string& name, const std::string& value);
	void setMap(const std::string& name, const std::map<std::string, int>& map);

private:
	static Settings* sInstance;

	Settings();

	void setDefaults();		//Clear everything and load default values.
	void saveRetropanguiConf();	// conf 에 존재하는 emulationstation.* 키를 현재 값으로 역기록 (saveFile 에서 호출)
	void processBackwardCompatibility();
	template<typename Map>
	void renameSetting(Map& map, std::string&& oldName, std::string&& newName);

	std::map<std::string, bool> mBoolMap;
	std::map<std::string, int> mIntMap;
	std::map<std::string, float> mFloatMap;
	std::map<std::string, std::string> mStringMap;
	std::map<std::string, std::map<std::string, int>> mMapIntMap;

	// retropangui.conf 에서 로드된 emulationstation.* 키 목록 (역기록 대상)
	std::set<std::string> mRetropanguiKeys;
};

#endif // ES_CORE_SETTINGS_H
