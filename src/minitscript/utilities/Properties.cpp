#include <minitscript/utilities/Properties.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include <minitscript/minitscript.h>
#include <minitscript/os/filesystem/FileSystem.h>
#include <minitscript/utilities/StringTools.h>

using std::sort;
using std::string;
using std::unordered_map;
using std::vector;

using minitscript::utilities::Properties;

using minitscript::os::filesystem::FileSystem;
using minitscript::utilities::StringTools;

void Properties::load(const string& pathName, const string& fileName)
{
	properties.clear();
	vector<string> lines;
	FileSystem::getContentAsStringArray(pathName, fileName, lines);
	for (auto i = 0; i < lines.size(); i++) {
		string line = StringTools::trim(lines[i]);
		if (line.length() == 0 || StringTools::startsWith(line, "#")) continue;
		auto separatorPos = line.find('=');
		if (separatorPos == -1) continue;
		string key = StringTools::substring(line, 0, separatorPos);
		string value = StringTools::substring(line, separatorPos + 1);
		properties[key] = value;
	}
}

void Properties::store(const string& pathName, const string& fileName) const {
	vector<string> result;
	int32_t idx = 0;
	for (const auto& [key, value]: properties) {
		result.push_back(key + "=" + value);
	}
	sort(result.begin(), result.end());
	FileSystem::setContentFromStringArray(pathName, fileName, result);
}
