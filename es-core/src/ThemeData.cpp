#include "ThemeData.h"

#include "components/ImageComponent.h"
#include "components/ScrollableContainer.h"
#include "components/TextComponent.h"
#include "math/Misc.h"
#include "renderers/Renderer.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"
#include "Log.h"
#include "platform.h"
#include "Settings.h"
#include <pugixml.hpp>
#include <algorithm>
#include <cstdio>

std::vector<std::string> ThemeData::sSupportedViews { { "system" }, { "basic" }, { "detailed" }, { "grid" }, { "video" } };
std::vector<std::string> ThemeData::sSupportedFeatures { { "video" }, { "carousel" }, { "z-index" }, { "visible" } };

std::map<std::string, std::map<std::string, ThemeData::ElementPropertyType>> ThemeData::sElementMap {
	{ "image", {
		{ "pos", RESOLUTION_PAIR },
		{ "size", RESOLUTION_PAIR },
		{ "maxSize", RESOLUTION_PAIR },
		{ "origin", NORMALIZED_PAIR },
		{ "rotation", FLOAT },
		{ "rotationOrigin", NORMALIZED_PAIR },
		{ "path", PATH },
		{ "default", PATH },
		{ "tile", BOOLEAN },
		{ "color", COLOR },
		{ "colorEnd", COLOR },
		{ "gradientType", STRING },
		{ "visible", BOOLEAN },
		{ "zIndex", FLOAT } } },
	{ "imagegrid", {
		{ "pos", RESOLUTION_PAIR },
		{ "size", RESOLUTION_PAIR },
		{ "margin", RESOLUTION_PAIR },
		{ "padding", RESOLUTION_RECT },
		{ "autoLayout", NORMALIZED_PAIR },
		{ "autoLayoutSelectedZoom", FLOAT },
		{ "gameImage", PATH },
		{ "folderImage", PATH },
		{ "imageSource", STRING },
		{ "scrollDirection", STRING },
		{ "centerSelection", BOOLEAN },
		{ "scrollLoop", BOOLEAN },
		{ "animate", BOOLEAN },
		{ "zIndex", FLOAT } } },
	{ "gridtile", {
		{ "size", RESOLUTION_PAIR },
		{ "padding", RESOLUTION_PAIR },
		{ "imageColor", COLOR },
		{ "backgroundImage", PATH },
		{ "backgroundCornerSize", RESOLUTION_PAIR },
		{ "backgroundColor", COLOR },
		{ "backgroundCenterColor", COLOR },
		{ "backgroundEdgeColor", COLOR } } },
	{ "text", {
		{ "pos", RESOLUTION_PAIR },
		{ "size", RESOLUTION_PAIR },
		{ "origin", NORMALIZED_PAIR },
		{ "rotation", FLOAT },
		{ "rotationOrigin", NORMALIZED_PAIR },
		{ "text", STRING },
		{ "backgroundColor", COLOR },
		{ "fontPath", PATH },
		{ "fontSize", RESOLUTION_FLOAT },
		{ "color", COLOR },
		{ "alignment", STRING },
		{ "verticalAlignment", STRING },
		{ "forceUppercase", BOOLEAN },
		{ "lineSpacing", FLOAT },
		{ "value", STRING },
		{ "scrollable", BOOLEAN },
		{ "visible", BOOLEAN },
		{ "zIndex", FLOAT } } },
	{ "textlist", {
		{ "pos", RESOLUTION_PAIR },
		{ "size", RESOLUTION_PAIR },
		{ "origin", NORMALIZED_PAIR },
		{ "selectorHeight", RESOLUTION_FLOAT },
		{ "selectorOffsetY", RESOLUTION_FLOAT },
		{ "selectorColor", COLOR },
		{ "selectorColorEnd", COLOR },
		{ "selectorGradientType", STRING },
		{ "selectorImagePath", PATH },
		{ "selectorImageTile", BOOLEAN },
		{ "selectedColor", COLOR },
		{ "primaryColor", COLOR },
		{ "secondaryColor", COLOR },
		{ "markerColor", COLOR },
		{ "fontPath", PATH },
		{ "fontSize", RESOLUTION_FLOAT },
		{ "scrollSound", PATH },
		{ "alignment", STRING },
		{ "horizontalMargin", RESOLUTION_FLOAT },
		{ "forceUppercase", BOOLEAN },
		{ "lineSpacing", FLOAT },
		{ "zIndex", FLOAT } } },
	{ "container", {
		{ "pos", RESOLUTION_PAIR },
		{ "size", RESOLUTION_PAIR },
	 	{ "origin", NORMALIZED_PAIR },
	 	{ "visible", BOOLEAN },
	 	{ "zIndex", FLOAT } } },
	{ "ninepatch", {
		{ "pos", RESOLUTION_PAIR },
		{ "size", RESOLUTION_PAIR },
		{ "path", PATH },
	 	{ "visible", BOOLEAN },
		{ "zIndex", FLOAT } } },
	{ "datetime", {
		{ "pos", RESOLUTION_PAIR },
		{ "size", RESOLUTION_PAIR },
		{ "origin", NORMALIZED_PAIR },
		{ "rotation", FLOAT },
		{ "rotationOrigin", NORMALIZED_PAIR },
		{ "backgroundColor", COLOR },
		{ "fontPath", PATH },
		{ "fontSize", RESOLUTION_FLOAT },
		{ "color", COLOR },
		{ "alignment", STRING },
		{ "forceUppercase", BOOLEAN },
		{ "lineSpacing", FLOAT },
		{ "value", STRING },
		{ "format", STRING },
		{ "displayRelative", BOOLEAN },
	 	{ "visible", BOOLEAN },
	 	{ "zIndex", FLOAT } } },
	{ "rating", {
		{ "pos", RESOLUTION_PAIR },
		{ "size", RESOLUTION_PAIR },
		{ "origin", NORMALIZED_PAIR },
		{ "rotation", FLOAT },
		{ "rotationOrigin", NORMALIZED_PAIR },
		{ "color", COLOR },
		{ "filledPath", PATH },
		{ "unfilledPath", PATH },
		{ "visible", BOOLEAN },
		{ "zIndex", FLOAT } } },
	{ "sound", {
		{ "path", PATH } } },
	{ "helpsystem", {
		{ "pos", RESOLUTION_PAIR },
		{ "origin", NORMALIZED_PAIR },
		{ "textColor", COLOR },
		{ "iconColor", COLOR },
		{ "fontPath", PATH },
		{ "fontSize", RESOLUTION_FLOAT } } },
	{ "video", {
		{ "pos", RESOLUTION_PAIR },
		{ "size", RESOLUTION_PAIR },
		{ "maxSize", RESOLUTION_PAIR },
		{ "origin", NORMALIZED_PAIR },
		{ "rotation", FLOAT },
		{ "rotationOrigin", NORMALIZED_PAIR },
		{ "default", PATH },
		{ "delay", FLOAT },
	 	{ "visible", BOOLEAN },
	 	{ "zIndex", FLOAT },
		{ "showSnapshotNoVideo", BOOLEAN },
		{ "showSnapshotDelay", BOOLEAN },
		{ "fadeTime", FLOAT } } },
	{ "carousel", {
		{ "type", STRING },
		{ "size", RESOLUTION_PAIR },
		{ "pos", RESOLUTION_PAIR },
		{ "origin", NORMALIZED_PAIR },
		{ "color", COLOR },
		{ "colorEnd", COLOR },
		{ "gradientType", STRING },
		{ "logoScale", FLOAT },
		{ "logoRotation", FLOAT },
		{ "logoRotationOrigin", NORMALIZED_PAIR },
		{ "logoSize", NORMALIZED_PAIR },
		{ "logoAlignment", STRING },
		{ "maxLogoCount", FLOAT },
		{ "zIndex", FLOAT } } }
};

#define MINIMUM_THEME_FORMAT_VERSION 3
#define CURRENT_THEME_FORMAT_VERSION 6

// helper
unsigned int getHexColor(const char* str)
{
	ThemeException error;
	if(!str)
		throw error << "Empty color";

	size_t len = strlen(str);
	if(len != 6 && len != 8)
		throw error << "Invalid color (bad length, \"" << str << "\" - must be 6 or 8)";

	unsigned int val;
	std::stringstream ss;
	ss << str;
	ss >> std::hex >> val;

	if(len == 6)
		val = (val << 8) | 0xFF;

	return val;
}

std::string ThemeData::resolvePlaceholders(const char* in)
{
	std::string inStr(in);

	if(inStr.empty())
		return inStr;

	const size_t variableBegin = inStr.find("${");
	const size_t variableEnd   = inStr.find("}", variableBegin);

	if((variableBegin == std::string::npos) || (variableEnd == std::string::npos))
		return inStr;

	std::string prefix  = inStr.substr(0, variableBegin);
	std::string replace = inStr.substr(variableBegin + 2, variableEnd - (variableBegin + 2));
	std::string suffix  = resolvePlaceholders(inStr.substr(variableEnd + 1).c_str());

	return prefix + mVariables[replace] + suffix;
}

ThemeData::ThemeData()
{
	mVersion = 0;
	mResolution = { 1, 1 };
}

void ThemeData::loadFile(std::map<std::string, std::string> sysDataMap, const std::string& path)
{
	mPaths.push_back(path);

	ThemeException error;
	error.setFiles(mPaths);

	if(!Utils::FileSystem::exists(path))
		throw error << "File does not exist!";

	mVersion = 0;
	mResolution = { 1, 1 };
	mViews.clear();
	mVariables.clear();

	mVariables.insert(sysDataMap.cbegin(), sysDataMap.cend());

	pugi::xml_document doc;
	pugi::xml_parse_result res = doc.load_file(path.c_str());
	if(!res)
		throw error << "XML parsing error: \n    " << res.description();

	pugi::xml_node root = doc.child("theme");
	if(!root)
		throw error << "Missing <theme> tag!";

	// parse version
	mVersion = root.child("formatVersion").text().as_float(-404);
	if(mVersion == -404)
		throw error << "<formatVersion> tag missing!\n   It's either out of date or you need to add <formatVersion>" << CURRENT_THEME_FORMAT_VERSION << "</formatVersion> inside your <theme> tag.";

	if(mVersion < MINIMUM_THEME_FORMAT_VERSION)
		throw error << "Theme uses format version " << mVersion << ". Minimum supported version is " << MINIMUM_THEME_FORMAT_VERSION << ".";

	// parse resolution
	std::string resolution = root.child("resolution").text().as_string("");

	if(resolution.size())
	{
		size_t divider = resolution.find(' ');

		if(divider != std::string::npos)
		{
			std::string w = resolution.substr(0, divider);
			std::string h = resolution.substr(divider, std::string::npos);

			mResolution.x() = (float)atof(w.c_str());
			mResolution.y() = (float)atof(h.c_str());
		}
	}

	parseVariables(root);
	parseIncludes(root);
	parseViews(root);
	parseFeatures(root);
}

void ThemeData::parseIncludes(const pugi::xml_node& root)
{
	ThemeException error;
	error.setFiles(mPaths);

	for(pugi::xml_node node = root.child("include"); node; node = node.next_sibling("include"))
	{
		std::string relPath = resolvePlaceholders(node.text().as_string());
		std::string path = Utils::FileSystem::resolveRelativePath(relPath, mPaths.back(), true, false);
		if(!ResourceManager::getInstance()->fileExists(path))
			throw error << "Included file \"" << relPath << "\" not found! (resolved to \"" << path << "\")";

		error << "    from included file \"" << relPath << "\":\n    ";

		mPaths.push_back(path);

		pugi::xml_document includeDoc;
		pugi::xml_parse_result result = includeDoc.load_file(path.c_str());
		if(!result)
			throw error << "Error parsing file: \n    " << result.description();

		pugi::xml_node theme = includeDoc.child("theme");
		if(!theme)
			throw error << "Missing <theme> tag!";

		parseVariables(theme);
		parseIncludes(theme);
		parseViews(theme);
		parseFeatures(theme);

		mPaths.pop_back();
	}
}

void ThemeData::parseFeatures(const pugi::xml_node& root)
{
	ThemeException error;
	error.setFiles(mPaths);

	for(pugi::xml_node node = root.child("feature"); node; node = node.next_sibling("feature"))
	{
		if(!node.attribute("supported"))
			throw error << "Feature missing \"supported\" attribute!";

		const std::string supportedAttr = node.attribute("supported").as_string();

		if (std::find(sSupportedFeatures.cbegin(), sSupportedFeatures.cend(), supportedAttr) != sSupportedFeatures.cend())
		{
			parseViews(node);
		}
	}
}

// <variables> 항목에 exec="명령어" 속성이 있으면 셸로 실행해 표준출력 첫 줄을
// 값으로 씀 - 테마가 (ES 코드/rpui 훅 없이) 파일시스템을 직접 들여다보고
// 동적인 값(예: 폴더 안 최신 파일 경로)을 스스로 결정할 수 있게 하는 범용
// 기능. 명령어 문자열 자체도 resolvePlaceholders를 거치므로 exec 안에서도
// ${system.name} 같은 다른 변수를 그대로 쓸 수 있음. 테마(theme.xml)는
// 어차피 신뢰된 번들 콘텐츠라 Scripting::fireEvent가 이미 system()으로 훅
// 스크립트를 실행하는 것과 같은 신뢰 모델 - 새로운 보안 경계 아님.
static std::string runShellCommandFirstLine(const std::string& cmd)
{
	if(cmd.empty())
		return "";

	FILE* pipe = popen(cmd.c_str(), "r");
	if(pipe == nullptr)
		return "";

	std::string result;
	char buf[1024];
	if(fgets(buf, sizeof(buf), pipe) != nullptr)
		result = buf;

	pclose(pipe);

	// 개행/공백 트리밍 (popen 출력은 보통 fgets가 끝에 '\n'을 포함시킴)
	while(!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' '))
		result.pop_back();

	return result;
}

void ThemeData::parseVariables(const pugi::xml_node& root)
{
	ThemeException error;
	error.setFiles(mPaths);

	pugi::xml_node variables = root.child("variables");

	if(!variables)
		return;

	for(pugi::xml_node_iterator it = variables.begin(); it != variables.end(); ++it)
	{
		std::string key = it->name();
		std::string val;

		pugi::xml_attribute execAttr = it->attribute("exec");
		if(execAttr)
		{
			std::string cmd = resolvePlaceholders(execAttr.value());
			val = runShellCommandFirstLine(cmd);
		}
		else
		{
			val = resolvePlaceholders(it->text().as_string());
		}

		if (!val.empty())
			mVariables.insert(std::pair<std::string, std::string>(key, val));
	}
}

void ThemeData::parseViews(const pugi::xml_node& root)
{
	ThemeException error;
	error.setFiles(mPaths);

	// parse views
	for(pugi::xml_node node = root.child("view"); node; node = node.next_sibling("view"))
	{
		if(!node.attribute("name"))
			throw error << "View missing \"name\" attribute!";

		const char* delim = " \t\r\n,";
		const std::string nameAttr = node.attribute("name").as_string();
		size_t prevOff = nameAttr.find_first_not_of(delim, 0);
		size_t off = nameAttr.find_first_of(delim, prevOff);
		std::string viewKey;
		while(off != std::string::npos || prevOff != std::string::npos)
		{
			viewKey = nameAttr.substr(prevOff, off - prevOff);
			prevOff = nameAttr.find_first_not_of(delim, off);
			off = nameAttr.find_first_of(delim, prevOff);

			if (std::find(sSupportedViews.cbegin(), sSupportedViews.cend(), viewKey) != sSupportedViews.cend())
			{
				ThemeView& view = mViews.insert(std::pair<std::string, ThemeView>(viewKey, ThemeView())).first->second;
				parseView(node, view);
			}
		}
	}
}

void ThemeData::parseView(const pugi::xml_node& root, ThemeView& view)
{
	ThemeException error;
	error.setFiles(mPaths);

	for(pugi::xml_node node = root.first_child(); node; node = node.next_sibling())
	{
		if(!node.attribute("name"))
			throw error << "Element of type \"" << node.name() << "\" missing \"name\" attribute!";

		auto elemTypeIt = sElementMap.find(node.name());
		if(elemTypeIt == sElementMap.cend())
			throw error << "Unknown element of type \"" << node.name() << "\"!";

		const char* delim = " \t\r\n,";
		const std::string nameAttr = node.attribute("name").as_string();
		size_t prevOff = nameAttr.find_first_not_of(delim, 0);
		size_t off =  nameAttr.find_first_of(delim, prevOff);
		while(off != std::string::npos || prevOff != std::string::npos)
		{
			std::string elemKey = nameAttr.substr(prevOff, off - prevOff);
			prevOff = nameAttr.find_first_not_of(delim, off);
			off = nameAttr.find_first_of(delim, prevOff);

			parseElement(node, elemTypeIt->second,
				view.elements.insert(std::pair<std::string, ThemeElement>(elemKey, ThemeElement())).first->second);

			if(std::find(view.orderedKeys.cbegin(), view.orderedKeys.cend(), elemKey) == view.orderedKeys.cend())
				view.orderedKeys.push_back(elemKey);
		}
	}
}


void ThemeData::parseElement(const pugi::xml_node& root, const std::map<std::string, ElementPropertyType>& typeMap, ThemeElement& element)
{
	ThemeException error;
	error.setFiles(mPaths);

	element.type = root.name();
	element.extra = root.attribute("extra").as_bool(false);

	for(pugi::xml_node node = root.first_child(); node; node = node.next_sibling())
	{
		auto typeIt = typeMap.find(node.name());
		if(typeIt == typeMap.cend())
			throw error << "Unknown property type \"" << node.name() << "\" (for element of type " << root.name() << ").";

		// RetroPangui: lang 속성이 있는 노드는 현재 ES 언어(Settings의 Language)와
		// 접두 매칭될 때만 적용한다. 언어 지정 없는 기본 노드는 항상 적용되므로,
		// 테마 XML에서 기본(예: 한국어) 노드를 먼저 쓰고 lang="en" 노드를 뒤에
		// 쓰면 언어가 맞을 때만 나중 값으로 덮어써지는 방식으로 동작함
		// (2026-07-19, 시스템 소개 문구 등 테마 콘텐츠의 언어별 전환 지원).
		pugi::xml_attribute langAttr = node.attribute("lang");
		if (langAttr)
		{
			std::string curLang = Settings::getInstance()->getString("Language");
			std::string wantLang = langAttr.as_string();
			if (curLang.compare(0, wantLang.size(), wantLang) != 0)
				continue;
		}

		std::string str = resolvePlaceholders(node.text().as_string());

		switch(typeIt->second)
		{
		case RESOLUTION_RECT:
		{
			Vector4f val;

			auto splits = Utils::String::delimitedStringToVector(str, " ");
			if (splits.size() == 2)
			{
				val = Vector4f((float)atof(splits.at(0).c_str()), (float)atof(splits.at(1).c_str()),
					(float)atof(splits.at(0).c_str()), (float)atof(splits.at(1).c_str()));
			}
			else if (splits.size() == 4)
			{
				val = Vector4f((float)atof(splits.at(0).c_str()), (float)atof(splits.at(1).c_str()),
					(float)atof(splits.at(2).c_str()), (float)atof(splits.at(3).c_str()));
			}

			element.properties[node.name()] = val / Vector4f(mResolution.x(), mResolution.y(), mResolution.x(), mResolution.y());
			break;
		}
		case RESOLUTION_PAIR:
		{
			size_t divider = str.find(' ');
			if(divider == std::string::npos)
				throw error << "invalid normalized pair (property \"" << node.name() << "\", value \"" << str.c_str() << "\")";

			std::string first = str.substr(0, divider);
			std::string second = str.substr(divider, std::string::npos);

			Vector2f val((float)atof(first.c_str()), (float)atof(second.c_str()));

			element.properties[node.name()] = val / mResolution;
			break;
		}
		case RESOLUTION_FLOAT:
		{
			float val = static_cast<float>(strtod(str.c_str(), 0));
			element.properties[node.name()] = val / mResolution.y();
			break;
		}
		case NORMALIZED_RECT:
		{
			Vector4f val;

			auto splits = Utils::String::delimitedStringToVector(str, " ");
			if (splits.size() == 2)
			{
				val = Vector4f((float)atof(splits.at(0).c_str()), (float)atof(splits.at(1).c_str()),
					(float)atof(splits.at(0).c_str()), (float)atof(splits.at(1).c_str()));
			}
			else if (splits.size() == 4)
			{
				val = Vector4f((float)atof(splits.at(0).c_str()), (float)atof(splits.at(1).c_str()),
					(float)atof(splits.at(2).c_str()), (float)atof(splits.at(3).c_str()));
			}

			element.properties[node.name()] = val;
			break;
		}
		case NORMALIZED_PAIR:
		{
			size_t divider = str.find(' ');
			if(divider == std::string::npos)
				throw error << "invalid normalized pair (property \"" << node.name() << "\", value \"" << str.c_str() << "\")";

			std::string first = str.substr(0, divider);
			std::string second = str.substr(divider, std::string::npos);

			Vector2f val((float)atof(first.c_str()), (float)atof(second.c_str()));

			element.properties[node.name()] = val;
			break;
		}
		case STRING:
			element.properties[node.name()] = str;
			break;
		case PATH:
		{
			std::string path = Utils::FileSystem::resolveRelativePath(str, mPaths.back(), true, false);
			if(!ResourceManager::getInstance()->fileExists(path))
			{
				std::stringstream ss;
				ss << "  Warning " << error.msg; // "from theme yadda yadda, included file yadda yadda
				ss << "could not find file \"" << node.text().get() << "\" ";
				if(node.text().get() != path)
					ss << "(which resolved to \"" << path << "\") ";
				LOG(LogWarning) << ss.str();
			}
			element.properties[node.name()] = path;
			break;
		}
		case COLOR:
			element.properties[node.name()] = getHexColor(str.c_str());
			break;
		case FLOAT:
		{
			float floatVal = static_cast<float>(strtod(str.c_str(), 0));
			element.properties[node.name()] = floatVal;
			break;
		}

		case BOOLEAN:
		{
			// only look at first char
			char first = str[0];
			// 1*, t* (true), T* (True), y* (yes), Y* (YES)
			bool boolVal = (first == '1' || first == 't' || first == 'T' || first == 'y' || first == 'Y');

			element.properties[node.name()] = boolVal;
			break;
		}
		default:
			throw error << "Unknown ElementPropertyType for \"" << root.attribute("name").as_string() << "\", property " << node.name();
		}
	}
}

bool ThemeData::hasView(const std::string& view)
{
	auto viewIt = mViews.find(view);
	return (viewIt != mViews.cend());
}

const ThemeData::ThemeElement* ThemeData::getElement(const std::string& view, const std::string& element, const std::string& expectedType) const
{
	auto viewIt = mViews.find(view);
	if(viewIt == mViews.cend())
		return NULL; // not found

	auto elemIt = viewIt->second.elements.find(element);
	if(elemIt == viewIt->second.elements.cend()) return NULL;

	if(elemIt->second.type != expectedType && !expectedType.empty())
	{
		LOG(LogWarning) << " requested mismatched theme type for [" << view << "." << element << "] - expected \""
			<< expectedType << "\", got \"" << elemIt->second.type << "\"";
		return NULL;
	}

	return &elemIt->second;
}

const std::shared_ptr<ThemeData>& ThemeData::getDefault()
{
	static std::shared_ptr<ThemeData> theme = nullptr;
	if(theme == nullptr)
	{
		theme = std::shared_ptr<ThemeData>(new ThemeData());

		const std::string path = Utils::FileSystem::getHomePath() + "/.emulationstation/es_theme_default.xml";
		if(Utils::FileSystem::exists(path))
		{
			try
			{
				std::map<std::string, std::string> emptyMap;
				theme->loadFile(emptyMap, path);
			} catch(ThemeException& e)
			{
				LOG(LogError) << e.what();
				theme = std::shared_ptr<ThemeData>(new ThemeData()); //reset to empty
			}
		}
	}

	return theme;
}

namespace
{
	// RetroPangui: scrollable="true" text extra 전용 래퍼.
	// 긴 글이 size 박스를 넘으면 클리핑 없이 다른 요소 위로 넘쳐 그려지는 문제 해결용.
	// 2026-07-17: 세로(ScrollableContainer 자동 스크롤) → 가로(우→좌) 마퀴로 재작성 -
	// BGM 제목 같은 한 줄 텍스트가 여러 줄로 접혀 위아래로 흐르는 게 어색하다는
	// 사용자 지적. 루프 파라미터/이중 렌더 방식은 TextListComponent 선택행 마퀴와 동일.
	class ScrollableTextExtra : public GuiComponent
	{
	public:
		ScrollableTextExtra(Window* window)
			: GuiComponent(window), mText(window),
			  mMarqueeTime(0), mMarqueeOffset(0), mMarqueeOffset2(0)
		{
		}

		void applyTheme(const std::shared_ptr<ThemeData>& theme, const std::string& view,
						const std::string& element, unsigned int properties) override
		{
			using namespace ThemeFlags;
			// 박스(위치/크기)는 이 컴포넌트가 갖고, 텍스트는 한 줄 자연폭(size 0,0)으로
			// 두어 넘침 여부를 폭 비교만으로 판정
			GuiComponent::applyTheme(theme, view, element, properties & (POSITION | ThemeFlags::SIZE | ORIGIN | ROTATION | Z_INDEX | VISIBLE));
			mText.applyTheme(theme, view, element, properties ^ (POSITION | ThemeFlags::SIZE | ORIGIN | ROTATION | Z_INDEX | VISIBLE));
			mText.setSize(0, 0);

		}

		// 내부 TextComponent로 위임 — 호출부가 TextComponent인지 ScrollableTextExtra인지
		// 몰라도 setValue()만으로 텍스트를 갱신할 수 있게 함 (bgmTitle 등 동적 텍스트 extra용).
		// SystemView가 매 프레임 같은 값으로 호출하므로 변경 시에만 반영(마퀴 리셋 방지).
		void setValue(const std::string& value) override
		{
			if (value == mText.getValue())
				return;
			mText.setValue(value);
			mText.setSize(0, 0);
			mMarqueeTime = 0;
			mMarqueeOffset = 0;
			mMarqueeOffset2 = 0;
		}
		std::string getValue() const override { return mText.getValue(); }

		void update(int deltaTime) override
		{
			GuiComponent::update(deltaTime);

			const float textLength = mText.getSize().x();
			const float limit = mSize.x();
			// 2026-07-17 사용자 지시: 길이와 무관하게 항상 우→좌로 흘림 -
			// "넘칠 때만 스크롤" 분기 제거. 아래 이중 렌더 루프 수식은 짧은
			// 텍스트에서도 그대로 성립한다(두 번째 사본이 returnLength 간격을
			// 두고 오른쪽에서 이어 들어오는 컨베이어가 됨).
			if (textLength <= 0 || !mText.getFont())
			{
				mMarqueeTime = 0;
				mMarqueeOffset = 0;
				mMarqueeOffset2 = 0;
				return;
			}

			// TextListComponent 선택행 마퀴와 동일한 속도/딜레이 공식
			const float speed        = mText.getFont()->sizeText("ABCDEFGHIJKLMNOPQRSTUVWXYZ").x() * 0.247f;
			const float delay        = 3000;
			const float scrollLength = textLength;
			const float returnLength = speed * 1.5f;
			const float scrollTime   = (scrollLength * 1000) / speed;
			const float returnTime   = (returnLength * 1000) / speed;
			const int   maxTime      = (int)(delay + scrollTime + returnTime);

			mMarqueeTime += deltaTime;
			while (mMarqueeTime > maxTime)
				mMarqueeTime -= maxTime;

			mMarqueeOffset = Math::Scroll::loop(delay, scrollTime + returnTime, (float)mMarqueeTime, scrollLength + returnLength);
			mMarqueeOffset2 = 0;
			if (mMarqueeOffset > (scrollLength - (limit - returnLength)))
				mMarqueeOffset2 = mMarqueeOffset - (scrollLength + returnLength);
		}

		void render(const Transform4x4f& parentTrans) override
		{
			if (!isVisible() || mText.getValue().empty())
				return;

			Transform4x4f trans = parentTrans * getTransform();
			const float textW = mText.getSize().x();
			const float yOff = (mSize.y() - mText.getSize().y()) / 2.0f;

			Renderer::pushClipRect(Vector2i((int)trans.translation().x(), (int)trans.translation().y()),
				Vector2i((int)mSize.x(), (int)mSize.y()));

			{
				Transform4x4f t = trans;
				t.translate(Vector3f(-mMarqueeOffset, yOff, 0));
				mText.render(t);
				if (mMarqueeOffset2 < 0)
				{
					// 꼬리가 빠져나가는 동안 두 번째 사본이 이어 들어옴 (루프)
					Transform4x4f t2 = trans;
					t2.translate(Vector3f(-mMarqueeOffset2, yOff, 0));
					mText.render(t2);
				}
			}

			Renderer::popClipRect();
		}

	private:
		TextComponent mText;
		int mMarqueeTime;
		float mMarqueeOffset;
		float mMarqueeOffset2;
	};
}

std::vector<std::pair<std::string, GuiComponent*>> ThemeData::makeExtras(const std::shared_ptr<ThemeData>& theme, const std::string& view, Window* window)
{
	std::vector<std::pair<std::string, GuiComponent*>> comps;

	auto viewIt = theme->mViews.find(view);
	if(viewIt == theme->mViews.cend())
		return comps;

	for(auto it = viewIt->second.orderedKeys.cbegin(); it != viewIt->second.orderedKeys.cend(); it++)
	{
		ThemeElement& elem = viewIt->second.elements.at(*it);
		if(elem.extra)
		{
			GuiComponent* comp = NULL;
			const std::string& t = elem.type;
			if(t == "image")
				comp = new ImageComponent(window);
			else if(t == "text")
			{
				if(elem.has("scrollable") && elem.get<bool>("scrollable"))
					comp = new ScrollableTextExtra(window);
				else
					comp = new TextComponent(window);
			}

			comp->setDefaultZIndex(10);
			comp->applyTheme(theme, view, *it, ThemeFlags::ALL);
			comps.push_back({ *it, comp });
		}
	}

	return comps;
}

std::map<std::string, ThemeSet> ThemeData::getThemeSets()
{
	std::map<std::string, ThemeSet> sets;

	static const size_t pathCount = 2;
	std::string paths[pathCount] =
	{
		"/etc/emulationstation/themes",
		Utils::FileSystem::getHomePath() + "/.emulationstation/themes"
	};

	for(size_t i = 0; i < pathCount; i++)
	{
		if(!Utils::FileSystem::isDirectory(paths[i]))
			continue;

		Utils::FileSystem::stringList dirContent = Utils::FileSystem::getDirContent(paths[i]);

		for(Utils::FileSystem::stringList::const_iterator it = dirContent.cbegin(); it != dirContent.cend(); ++it)
		{
			if(Utils::FileSystem::isDirectory(*it))
			{
				ThemeSet set = {*it};
				sets[set.getName()] = set;
			}
		}
	}

	return sets;
}

std::string ThemeData::getThemeFromCurrentSet(const std::string& system)
{
	std::map<std::string, ThemeSet> themeSets = ThemeData::getThemeSets();
	if(themeSets.empty())
	{
		// no theme sets available
		return "";
	}

	std::map<std::string, ThemeSet>::const_iterator set = themeSets.find(Settings::getInstance()->getString("ThemeSet"));
	if(set == themeSets.cend())
	{
		// currently selected theme set is missing, so just pick the first available set
		set = themeSets.cbegin();
		Settings::getInstance()->setString("ThemeSet", set->first);
	}

	return set->second.getThemePath(system);
}
