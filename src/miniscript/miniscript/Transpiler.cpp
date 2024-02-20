#include <miniscript/miniscript/Transpiler.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <miniscript/miniscript.h>
#include <miniscript/math/Math.h>
#include <miniscript/os/filesystem/FileSystem.h>
#include <miniscript/miniscript/MiniScript.h>
#include <miniscript/miniscript/Version.h>
#include <miniscript/utilities/Character.h>
#include <miniscript/utilities/Console.h>
#include <miniscript/utilities/Exception.h>
#include <miniscript/utilities/Integer.h>
#include <miniscript/utilities/StringTools.h>

using miniscript::miniscript::Transpiler;

using std::find;
using std::remove;
using std::sort;
using std::string;
using std::string_view;
using std::to_string;
using std::unique;
using std::unordered_map;
using std::unordered_set;
using std::vector;

using miniscript::math::Math;
using miniscript::os::filesystem::FileSystem;
using miniscript::miniscript::MiniScript;
using miniscript::miniscript::Transpiler;
using miniscript::miniscript::Version;
using miniscript::utilities::Character;
using miniscript::utilities::Console;
using miniscript::utilities::Exception;
using miniscript::utilities::Integer;
using miniscript::utilities::StringTools;

void Transpiler::transpile(MiniScript* miniScript, const string& transpilationFileName, const vector<string>& miniScriptExtensionFileNames) {
	auto scriptFileName = miniScript->getScriptPathName() + "/" + miniScript->getScriptFileName();
	//
	Console::printLine(scriptFileName + ": Processing script");
	//
	auto replace = [](const vector<string> input, const string& startTag, const string& endTag, const string& replacement, vector<string>& output) -> bool {
		auto reject = false;
		auto replaceSuccess = false;
		for (auto i = 0; i < input.size(); i++) {
			const auto& line = input[i];
			auto trimmedLine = StringTools::trim(line);
			if (StringTools::startsWith(trimmedLine, "//") == true) {
				if (reject == false) output.push_back(line);
				continue;
			}
			if (trimmedLine == startTag) {
				reject = true;
				output.push_back(line);
			} else
			if (trimmedLine == endTag) {
				reject = false;
				replaceSuccess = true;
				output.push_back(replacement);
				output.push_back(line);
			} else {
				if (reject == false) output.push_back(line);
			}
		}
		//
		return replaceSuccess;
	};

	//
	unordered_map<string, vector<string>> methodCodeMap;
	auto allMethods = getAllClassesMethodNames(miniScript);

	//
	vector<string> transpilationUnitIncludes;
	vector<string> transpilationUnitUsings;

	//
	vector<string> transpilationUnits;
	for (const auto& transpilationUnit: miniScript->getTranspilationUnits()) transpilationUnits.push_back(transpilationUnit);
	for (const auto& transpilationUnit: miniScriptExtensionFileNames) transpilationUnits.push_back(transpilationUnit);
	for (const auto& transpilationUnit: transpilationUnits) {
		vector<string> transpilationUnitCode;
		FileSystem::getContentAsStringArray(FileSystem::getPathName(transpilationUnit), FileSystem::getFileName(transpilationUnit), transpilationUnitCode);
		for (auto i = 0; i < transpilationUnitCode.size(); i++) {
			const auto& line = transpilationUnitCode[i];
			auto trimmedLine = StringTools::trim(line);
			if (StringTools::startsWith(trimmedLine, "#include ") == true) {
				transpilationUnitIncludes.push_back(trimmedLine);
			} else
			if (StringTools::startsWith(trimmedLine, "using ") == true) {
				transpilationUnitUsings.push_back(trimmedLine);
			}
			if (StringTools::startsWith(trimmedLine, "registerMethod") == true ||
				StringTools::startsWith(trimmedLine, "miniScript->registerMethod") == true) {
				auto bracketCount = 0;
				string className;
				if (StringTools::firstIndexOf(StringTools::substring(trimmedLine, 14), "new") == string::npos) {
					Console::printLine(transpilationUnit + ": registerMethod @ " + to_string(i) + ": '" + trimmedLine + "': unable to determine class name");
				} else {
					auto classNameStartIdx = trimmedLine.find("registerMethod") + 14 + 5;
					for (auto j = classNameStartIdx; j < trimmedLine.size(); j++) {
						auto c = trimmedLine[j];
						if (c == '(') break;
						if (c == ' ') continue;
						className+= c;
					}
					gatherMethodCode(transpilationUnitCode, className, i, methodCodeMap);
				}
			}
		}
	}
	//
	sort(transpilationUnitIncludes.begin(), transpilationUnitIncludes.end());
	transpilationUnitIncludes.erase(unique(transpilationUnitIncludes.begin(), transpilationUnitIncludes.end()), transpilationUnitIncludes.end());

	//
	sort(transpilationUnitUsings.begin(), transpilationUnitUsings.end());
	transpilationUnitUsings.erase(unique(transpilationUnitUsings.begin(), transpilationUnitUsings.end()), transpilationUnitUsings.end());

	//
	Console::printLine(miniScript->getInformation());

	//
	const auto& scripts = miniScript->getScripts();

	// determine variables
	unordered_set<string> globalVariables;
	vector<unordered_set<string>> localVariables(scripts.size());
	determineVariables(miniScript, globalVariables, localVariables);

	//
	string declarationIndent = "\t";
	string definitionIndent = "\t";
	string generatedExecuteCode;
	{
		auto scriptIdx = 0;
		for (const auto& script: scripts) {
			auto methodName = createMethodName(miniScript, scriptIdx);
			generatedExecuteCode+= declarationIndent + "\t\t" + "if (getScriptState().scriptIdx == " + to_string(scriptIdx) + ") " + methodName + "(scriptState.statementIdx); else\n";
			scriptIdx++;
		}
	}
	if (generatedExecuteCode.empty() == false) generatedExecuteCode+= declarationIndent + "\t\t\t" + ";\n";

	// determine "initialize" and "nothing" script indices
	auto initializeScriptIdx = -1;
	auto nothingScriptIdx = -1;
	{
		auto scriptIdx = 0;
		for (const auto& script: scripts) {
			//
			if (StringTools::regexMatch(script.condition, "[a-zA-Z0-9_]+") == true) {
				if (script.condition == "nothing") nothingScriptIdx = scriptIdx;
				if (script.condition == "initialize") initializeScriptIdx = scriptIdx;
			}
			//
			scriptIdx++;
		}
	}

	// member access evaluation
	vector<string> memberAccessEvaluationDeclarations;
	vector<string> memberAccessEvaluationDefinitions;
	generateEvaluateMemberAccessArrays(miniScript, memberAccessEvaluationDeclarations, memberAccessEvaluationDefinitions);

	//
	string miniScriptClassName = FileSystem::getFileName(transpilationFileName);
	string generatedDeclarations = "\n";
	generatedDeclarations+= declarationIndent + "/**" + "\n";
	generatedDeclarations+= declarationIndent + " * Public destructor" + "\n";
	generatedDeclarations+= declarationIndent + " */" + "\n";
	generatedDeclarations+= declarationIndent + "inline ~" + miniScriptClassName + "() {" + "\n";
	for (const auto& variable: globalVariables) {
		generatedDeclarations+= declarationIndent + "\t" + createGlobalVariableName(variable) + ".unset();" + "\n";
	}
	{
		auto scriptIdx = 0;
		for (const auto& script: scripts) {
			if (script.type == MiniScript::Script::TYPE_ON ||
				script.type == MiniScript::Script::TYPE_ONENABLED ||
				script.type == MiniScript::Script::TYPE_STACKLET) {
				scriptIdx++;
				continue;
			}
			auto methodName = createMethodName(miniScript, scriptIdx);
			auto shortMethodName = createShortMethodName(miniScript, scriptIdx);
			generatedDeclarations+= declarationIndent + "\t" + "if (" + shortMethodName + "_Stack.empty() == false) _Console::printLine(\"~" + miniScriptClassName + "(): Warning: " + methodName + ": stack not empty, size = \" + to_string(" + shortMethodName + "_Stack.size()));" + "\n";
			scriptIdx++;
		}
	}

	generatedDeclarations+= declarationIndent + "}" + "\n";
	generatedDeclarations+= "\n";
	generatedDeclarations+= declarationIndent + "// overridden methods" + "\n";
	generatedDeclarations+= declarationIndent + "void registerMethods() override;" + "\n";
	generatedDeclarations+= declarationIndent + "inline void registerVariables() override {" + "\n";
	if (globalVariables.empty() == false) {
		for (const auto& variable: globalVariables) {
			generatedDeclarations+= declarationIndent + "\t" + createGlobalVariableName(variable) + ".unset();" + "\n";
		}
		generatedDeclarations+= declarationIndent + "\t" + "//" + "\n";
	}
	generatedDeclarations+= declarationIndent + "\t" + miniScript->getBaseClass() + "::registerVariables();" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "// global script variables" + "\n";
	for (const auto& variable: globalVariables) {
		generatedDeclarations+= declarationIndent + "\t" + "if (hasVariable(\"" + variable + "\") == false) setVariable(\"" + variable + "\", Variable())" + ";" + "\n";
		generatedDeclarations+= declarationIndent + "\t" + createGlobalVariableName(variable) + " = getVariable(\"" + variable + "\", nullptr, true);" + "\n";
	}
	generatedDeclarations+= declarationIndent + "}" + "\n";
	generatedDeclarations+= declarationIndent + "void emit(const string& condition) override;" + "\n";
	generatedDeclarations+= declarationIndent + "inline void startScript() override {" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "if (native == false) {" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "\t" + miniScript->getBaseClass() + "::startScript();" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "\t" + "return;" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "}" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "getScriptState().running = true;" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "registerVariables();" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "resetScriptExecutationState(" + to_string(initializeScriptIdx) + ", STATEMACHINESTATE_NEXT_STATEMENT);" + "\n";
	generatedDeclarations+= declarationIndent + "}" + "\n";
	generatedDeclarations+= declarationIndent + "inline void execute() override {" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "if (native == false) {" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "\t" + miniScript->getBaseClass() + "::execute();" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "\t" + "return;" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "}" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "auto& scriptState = getScriptState();" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "if (scriptState.running == false) return;" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "// execute while having statements to be processed" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "if (scriptState.state == STATEMACHINESTATE_NEXT_STATEMENT) {" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "\t" + "// take goto statement index into account" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "\t" + "if (scriptState.gotoStatementIdx != STATEMENTIDX_NONE) {" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "\t" + "\t" + "scriptState.statementIdx = scriptState.gotoStatementIdx;" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "\t" + "\t" + "scriptState.gotoStatementIdx = STATEMENTIDX_NONE;" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "\t" + "}" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "\t" + "//" + "\n";
	generatedDeclarations+= generatedExecuteCode;
	generatedDeclarations+= declarationIndent + "\t" + "}" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "if (getScriptState().running == false) return;" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "if (isFunctionRunning() == false && deferredEmit.empty() == false) {" + "\n";
	generatedDeclarations+= declarationIndent + "\t\t" + "auto condition = deferredEmit;" + "\n";
	generatedDeclarations+= declarationIndent + "\t\t" + "deferredEmit.clear();" + "\n";
	generatedDeclarations+= declarationIndent + "\t\t" + "emit(condition);" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "}" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "if (getScriptState().running == false) return;" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "executeStateMachine();" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "// try garbage collection" + "\n";
	generatedDeclarations+= declarationIndent + "\t" + "tryGarbageCollection();" + "\n";
	generatedDeclarations+= declarationIndent + "}" + "\n";
	generatedDeclarations+= "\n";
	generatedDeclarations+= string() + "protected:" + "\n";

	//
	for (const auto& memberAccessEvaluationDeclaration: memberAccessEvaluationDeclarations) {
		generatedDeclarations+= declarationIndent + memberAccessEvaluationDeclaration + "\n";
	}
	generatedDeclarations+= "\n";

	//
	generatedDeclarations+= declarationIndent + "// overridden methods" + "\n";
	generatedDeclarations+= declarationIndent + "void initializeNative() override;" + "\n";
	generatedDeclarations+= declarationIndent + "int determineScriptIdxToStart() override;" + "\n";
	generatedDeclarations+= declarationIndent + "int determineNamedScriptIdxToStart() override;" + "\n";
	generatedDeclarations+= "\n";

	string registerMethodsDefinitions;
	registerMethodsDefinitions+= "void " + miniScriptClassName + "::registerMethods() {" + "\n";
	registerMethodsDefinitions+= definitionIndent + miniScript->getBaseClass() + "::registerMethods();" + "\n";
	registerMethodsDefinitions+= definitionIndent + "if (native == false) return;" + "\n";
	//
	for (const auto& memberAccessEvaluationDefintion: memberAccessEvaluationDefinitions) {
		registerMethodsDefinitions+= definitionIndent + memberAccessEvaluationDefintion + "\n";
	}
	registerMethodsDefinitions+= string() + "}" + "\n";

	//
	string emitDefinition;
	emitDefinition+= "void " + miniScriptClassName + "::emit(const string& condition) {" + "\n";
	emitDefinition+= definitionIndent + "if (native == false) {" + "\n";
	emitDefinition+= definitionIndent + "\t" + miniScript->getBaseClass() + "::emit(condition);" + "\n";
	emitDefinition+= definitionIndent + "\t" + "return;" + "\n";
	emitDefinition+= definitionIndent + "}" + "\n";
	emitDefinition+= definitionIndent + "if (isFunctionRunning() == true) {" + "\n";
	emitDefinition+= definitionIndent + "\t" + "deferredEmit = condition;" + "\n";
	emitDefinition+= definitionIndent + "\t" + "return;" + "\n";
	emitDefinition+= definitionIndent + "}" + "\n";
	emitDefinition+= definitionIndent + "emitted = true;" + "\n";

	//
	string generatedDefinitions = "\n";
	string initializeNativeDefinition;
	initializeNativeDefinition+= "void " + miniScriptClassName + "::initializeNative() {" + "\n";
	initializeNativeDefinition+= definitionIndent + "setNative(true);" + "\n";
	initializeNativeDefinition+= definitionIndent + "setNativeHash(\"" + miniScript->getNativeHash() + "\");" + "\n";
	initializeNativeDefinition+= definitionIndent + "setNativeScripts(" + "\n";
	initializeNativeDefinition+= definitionIndent + "\t" + "{" + "\n";
	{
		auto scriptIdx = 0;
		for (const auto& script: scripts) {
			initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "Script(" + "\n";
			initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + getScriptTypeEnumIdentifier(script.type) + "," + "\n";
			initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + to_string(script.line) + "," + "\n";
			initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + "\"" + StringTools::replace(StringTools::replace(script.condition, "\\", "\\\\"), "\"", "\\\"") + "\"," + "\n";
			initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + "\"" + StringTools::replace(StringTools::replace(script.executableCondition, "\\", "\\\\"), "\"", "\\\"") + "\"," + "\n";
			initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + "Statement(" + "\n";
			initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + "\t" + to_string(script.conditionStatement.line) + "," + "\n";
			initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + "\t" + to_string(script.conditionStatement.statementIdx) + "," + "\n";
			initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + "\t" + "\"" + StringTools::replace(StringTools::replace(StringTools::replace(script.conditionStatement.statement, "\\", "\\\\"), "\"", "\\\""), "\n", "\\n") + "\"," + "\n";
			initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + "\t" + "\"" + StringTools::replace(StringTools::replace(StringTools::replace(script.conditionStatement.executableStatement, "\\", "\\\\"), "\"", "\\\""), "\n", "\\n") + "\"," + "\n";
			initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + "\t" + to_string(script.conditionStatement.gotoStatementIdx) + "\n";
			initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + ")," + "\n";
			initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + "{}," + "\n";
			initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + "\"" + script.name + "\"," + "\n";
			initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + (script.emitCondition == true?"true":"false") + "," + "\n";
			initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + "{" + "\n";
			auto statementIdx = MiniScript::STATEMENTIDX_FIRST;
			for (const auto& statement: script.statements) {
				initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + "\t" + "Statement(" + "\n";
				initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + "\t" + "\t" + to_string(statement.line) + "," + "\n";
				initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + "\t" + "\t" + to_string(statement.statementIdx) + "," + "\n";
				initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + "\t" + "\t" + "\"" + StringTools::replace(StringTools::replace(StringTools::replace(statement.statement, "\\", "\\\\"), "\n", "\\n"), "\"", "\\\"") + "\"," + "\n";
				initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + "\t" + "\t" + "\"" + StringTools::replace(StringTools::replace(StringTools::replace(statement.executableStatement, "\\", "\\\\"), "\n", "\\n"), "\"", "\\\"") + "\"," + "\n";
				initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + "\t" + "\t" + to_string(statement.gotoStatementIdx) + "\n";
				initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + "\t" + ")" + (statementIdx < script.statements.size() - 1?",":"") + "\n";
				statementIdx++;
			}
			initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + "}," + "\n";
			initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + "{},\n";
			initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + (script.callable == true?"true":"false") + ",\n";
			initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + "{\n";
			auto argumentIdx = 0;
			for (const auto& argument: script.arguments) {
				initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + "\t" + "Script::Argument(" + "\n";
				initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + "\t" + "\t" + "\"" + argument.name + "\"," + "\n";
				initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + "\t" + "\t" + (argument.reference == true?"true":"false") + "\n";
				initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + "\t" ")" + (argumentIdx != script.arguments.size() - 1?",":"") + "\n";
				argumentIdx++;
			}
			initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + "}\n";
			initializeNativeDefinition+= definitionIndent + "\t" + "\t" + ")" + (scriptIdx < scripts.size() - 1?",":"") + "\n";
			scriptIdx++;
		}
	}
	initializeNativeDefinition+= definitionIndent + "\t" + "}" + "\n";
	initializeNativeDefinition+= definitionIndent + ");" + "\n";
	initializeNativeDefinition+= definitionIndent + "setNativeFunctions(" + "\n";
	initializeNativeDefinition+= definitionIndent + "\t" + "{" + "\n";
	auto functionItIdx = 0;
	for (const auto& [functionName, functionScriptIdx]: miniScript->functions) {
		initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "{" + "\n";
		initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + "\"" + functionName + "\"," + "\n";
		initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "\t" + to_string(functionScriptIdx) + "\n";
		initializeNativeDefinition+= definitionIndent + "\t" + "\t" + "}" + (functionItIdx != miniScript->functions.size() - 1?",":"") + "\n";
		functionItIdx++;
	}
	initializeNativeDefinition+= definitionIndent + "\t" + "}" + "\n";
	initializeNativeDefinition+= definitionIndent + ");" + "\n";
	initializeNativeDefinition+= string() + "}" + "\n";

	//
	string generatedDetermineScriptIdxToStartDefinition = "\n";
	generatedDetermineScriptIdxToStartDefinition+= "int " + miniScriptClassName + "::determineScriptIdxToStart() {" + "\n";
	generatedDetermineScriptIdxToStartDefinition+= definitionIndent + "#define MINISCRIPT_METHOD_POPSTACK()" + "\n";
	generatedDetermineScriptIdxToStartDefinition+= definitionIndent + "if (native == false) return MiniScript::determineScriptIdxToStart();" + "\n";
	generatedDetermineScriptIdxToStartDefinition+= definitionIndent + "// MiniScript setup" + "\n";
	generatedDetermineScriptIdxToStartDefinition+= definitionIndent + "auto miniScript = this;" + "\n";
	generatedDetermineScriptIdxToStartDefinition+= definitionIndent + "//" + "\n";
	string generatedDetermineNamedScriptIdxToStartDefinition = "\n";
	generatedDetermineNamedScriptIdxToStartDefinition+= "int " + miniScriptClassName + "::determineNamedScriptIdxToStart() {" + "\n";
	generatedDetermineNamedScriptIdxToStartDefinition+= definitionIndent + "#define MINISCRIPT_METHOD_POPSTACK()" + "\n";
	generatedDetermineNamedScriptIdxToStartDefinition+= definitionIndent + "if (native == false) return MiniScript::determineNamedScriptIdxToStart();" + "\n";
	generatedDetermineNamedScriptIdxToStartDefinition+= definitionIndent + "auto miniScript = this;" + "\n";
	generatedDetermineNamedScriptIdxToStartDefinition+= definitionIndent + "for (const auto& enabledNamedCondition: enabledNamedConditions) {" + "\n";
	{
		auto scriptIdx = 0;
		for (const auto& script: scripts) {
			// method name
			auto methodName = createMethodName(miniScript, scriptIdx);
			auto shortMethodName = createShortMethodName(miniScript, scriptIdx);

			// emit name
			string emitName =
				(script.name.empty() == false?script.name:(
					StringTools::regexMatch(script.condition, "[a-zA-Z0-9_]+") == true?
						script.condition:
						to_string(scriptIdx)
					)
				);

			// emit code
			if (script.type == MiniScript::Script::TYPE_ON && StringTools::regexMatch(script.condition, "[a-zA-Z0-9_]+") == true) {
				string emitDefinitionIndent = "\t";
				emitDefinition+= emitDefinitionIndent + "if (condition == \"" + emitName + "\") {" + "\n";
				emitDefinition+= emitDefinitionIndent + "\t" + "resetScriptExecutationState(" + to_string(scriptIdx) + ", STATEMACHINESTATE_NEXT_STATEMENT);" + "\n";
				emitDefinition+= emitDefinitionIndent + "} else" + "\n";
			}

			// declaration
			generatedDeclarations+= declarationIndent + "/**" + "\n";
			generatedDeclarations+= declarationIndent + " * Miniscript transpilation of: " + getScriptTypeReadableName(script.type) + ": " + script.condition + (script.name.empty() == false?" (" + script.name + ")":"") + "\n";
			generatedDeclarations+= declarationIndent + " * @param miniScriptGotoStatementIdx MiniScript goto statement index" + "\n";
			generatedDeclarations+= declarationIndent + " */" + "\n";
			generatedDeclarations+= declarationIndent + "void " + methodName + "(int miniScriptGotoStatementIdx);" + "\n";
			generatedDeclarations+= "\n";

			// transpile definition
			generatedDefinitions+= "void " + miniScriptClassName + "::" + methodName + "(int miniScriptGotoStatementIdx) {" + "\n";
			string generatedSubCode;

			// local variables, which applies to stacklets and functions
			if (localVariables[scriptIdx].empty() == false) {
				generatedDefinitions+= definitionIndent + "// local script variables" + "\n";
				generatedDefinitions+= definitionIndent + "#define MINISCRIPT_METHOD_POPSTACK() " + shortMethodName + "_Stack.pop();" + "\n";
				generatedDefinitions+= definitionIndent + "// STATEMENTIDX_FIRST means complete method call" + "\n";
				generatedDefinitions+= definitionIndent + "if (miniScriptGotoStatementIdx == STATEMENTIDX_FIRST) {" + "\n";
				generatedDefinitions+= definitionIndent + "\t" + "auto& _lv = " + shortMethodName + "_Stack.emplace();" + "\n";
				for (const auto& variable: localVariables[scriptIdx]) {
					generatedDefinitions+= definitionIndent + "\t" + "if (hasVariable(\"" + variable + "\") == false) setVariable(\"" + variable + "\", Variable())" + "; _lv." + createLocalVariableName(variable) + " = getVariable(\"" + variable + "\", nullptr, true);" + "\n";
				}
				generatedDefinitions+= definitionIndent + "}" + "\n";
				generatedDefinitions+= definitionIndent + "//" + "\n";
				generatedDefinitions+= definitionIndent + "auto& _lv = " + shortMethodName + "_Stack.top();" + "\n";
			} else {
				// we can still have a stacklet without local variables
				if (script.type == MiniScript::Script::TYPE_STACKLET) {
					auto scopeScriptIdx = miniScript->getStackletScopeScriptIdx(scriptIdx);
					if (scopeScriptIdx != MiniScript::SCRIPTIDX_NONE) {
						auto scopeShortMethodName = createShortMethodName(miniScript, miniScript->getStackletScopeScriptIdx(scriptIdx));
						generatedDefinitions+= definitionIndent + "// local script variables" + "\n";
						generatedDefinitions+= definitionIndent + "auto& _lv = " + scopeShortMethodName + "_Stack.top();" + "\n";
					}
				} else
				// ok, reset emitted for conditions and enabled conditions
				if (script.type == MiniScript::Script::TYPE_ON ||
					script.type == MiniScript::Script::TYPE_ONENABLED) {
					generatedDefinitions+= definitionIndent + "// reset emitted" + "\n";
					generatedDefinitions+= definitionIndent + "emitted = false;" + "\n";
				}
				// no local variables or condition/enabled condition
				generatedDefinitions+= definitionIndent + "// local script variables" + "\n";
				generatedDefinitions+= definitionIndent + "#define MINISCRIPT_METHOD_POPSTACK()" + "\n";
			}

			// transpile array access operator and map/set initializer
			string arrayAccessMethodsDefinitions;
			string arrayMapSetInitializerDefinitions;

			//
			for (auto statementIdx = 0; statementIdx < script.statements.size(); statementIdx++) {
				string statementArrayMapSetInitializerDefinitions;
				//
				generateArrayMapSetInitializer(
					miniScript,
					statementArrayMapSetInitializerDefinitions,
					MiniScript::SCRIPTIDX_NONE,
					script.type == MiniScript::Script::TYPE_FUNCTION || script.type == MiniScript::Script::TYPE_STACKLET?
						scriptIdx:
						MiniScript::SCRIPTIDX_NONE,
					miniScriptClassName,
					methodName,
					script.syntaxTree[statementIdx],
					script.statements[statementIdx],
					methodCodeMap,
					allMethods,
					false,
					{},
					1
				);
				if (statementArrayMapSetInitializerDefinitions.empty() == false) statementArrayMapSetInitializerDefinitions+= "\n";
				arrayMapSetInitializerDefinitions+= statementArrayMapSetInitializerDefinitions;
				//
				generateArrayAccessMethods(
					miniScript,
					arrayAccessMethodsDefinitions,
					miniScriptClassName,
					MiniScript::SCRIPTIDX_NONE,
					script.type == MiniScript::Script::TYPE_FUNCTION || script.type == MiniScript::Script::TYPE_STACKLET?
						scriptIdx:
						MiniScript::SCRIPTIDX_NONE,
					methodName,
					script.syntaxTree[statementIdx],
					script.statements[statementIdx],
					methodCodeMap,
					allMethods,
					false,
					{},
					1
				);
			}

			//
			generatedDefinitions+= arrayMapSetInitializerDefinitions;
			generatedDefinitions+= arrayAccessMethodsDefinitions;

			//
			transpile(miniScript, generatedSubCode, scriptIdx, methodCodeMap, allMethods);
			generatedDefinitions+= generatedSubCode;

			// local variables
			if (localVariables[scriptIdx].empty() == false) {
				generatedDefinitions+= definitionIndent + "//" + "\n";
				generatedDefinitions+= definitionIndent + "MINISCRIPT_METHOD_POPSTACK();" + "\n";
			}
			generatedDefinitions+= definitionIndent + "#undef MINISCRIPT_METHOD_POPSTACK" + "\n";

			//
			generatedDefinitions+= string() + "}" + "\n\n";

			//
			if (script.emitCondition == false) {
				if (script.type == MiniScript::Script::TYPE_ONENABLED) {
					generatedDetermineNamedScriptIdxToStartDefinition+= definitionIndent;
					generatedDetermineNamedScriptIdxToStartDefinition+= definitionIndent + "\t" + "// next statements belong to tested enabled named condition with name \"" + script.name + "\"" + "\n";
					generatedDetermineNamedScriptIdxToStartDefinition+= definitionIndent + "\t" + "if (enabledNamedCondition == \"" + script.name + "\") {" + "\n";
				} else {
					generatedDetermineScriptIdxToStartDefinition+= definitionIndent + "{" + "\n";
				}
				//
				string arrayMapSetInitializerDefinitions;
				string arrayAccessMethodsDefinitions;
				//
				generateArrayMapSetInitializer(
					miniScript,
					arrayMapSetInitializerDefinitions,
					MiniScript::SCRIPTIDX_NONE,
					MiniScript::SCRIPTIDX_NONE,
					miniScriptClassName,
					methodName,
					script.conditionSyntaxTree,
					script.conditionStatement,
					methodCodeMap,
					allMethods,
					true,
					{},
					script.type == MiniScript::Script::TYPE_ONENABLED?3:2
				);
				if (arrayMapSetInitializerDefinitions.empty() == false) arrayMapSetInitializerDefinitions+= "\n";
				//
				generateArrayAccessMethods(
					miniScript,
					arrayAccessMethodsDefinitions,
					miniScriptClassName,
					MiniScript::SCRIPTIDX_NONE,
					MiniScript::SCRIPTIDX_NONE,
					methodName,
					script.conditionSyntaxTree,
					script.conditionStatement,
					methodCodeMap,
					allMethods,
					true,
					{},
					script.type == MiniScript::Script::TYPE_ONENABLED?3:2
				);
				//
				if (script.type == MiniScript::Script::TYPE_ON) {
					generatedDetermineScriptIdxToStartDefinition+= arrayMapSetInitializerDefinitions;
					generatedDetermineScriptIdxToStartDefinition+= arrayAccessMethodsDefinitions;
				} else {
					generatedDetermineNamedScriptIdxToStartDefinition+= arrayMapSetInitializerDefinitions;
					generatedDetermineNamedScriptIdxToStartDefinition+= arrayAccessMethodsDefinitions;
				}
				//
				transpileCondition(
					miniScript,
					script.type == MiniScript::Script::TYPE_ON?generatedDetermineScriptIdxToStartDefinition:generatedDetermineNamedScriptIdxToStartDefinition,
					scriptIdx,
					methodCodeMap,
					allMethods,
					"-1",
					"bool returnValueBool; returnValue.getBooleanValue(returnValueBool); if (returnValueBool == true) return " + to_string(scriptIdx) + ";",
					script.type == MiniScript::Script::TYPE_ONENABLED?1:0
				);
				//
				if (script.type == MiniScript::Script::TYPE_ONENABLED) {
					generatedDetermineNamedScriptIdxToStartDefinition+= definitionIndent + "\t" + "}" + "\n";
				} else {
					generatedDetermineScriptIdxToStartDefinition+= definitionIndent + "}" + "\n";
				}
			}

			//
			scriptIdx++;
		}
	}

	//
	generatedDetermineScriptIdxToStartDefinition+= "\n";
	generatedDetermineScriptIdxToStartDefinition+= definitionIndent + "//" + "\n";
	generatedDetermineScriptIdxToStartDefinition+= definitionIndent + "return " + to_string(nothingScriptIdx) + ";" + "\n";
	generatedDetermineScriptIdxToStartDefinition+= definitionIndent + "#undef MINISCRIPT_METHOD_POPSTACK" + "\n";
	generatedDetermineScriptIdxToStartDefinition+= string() + "}" + "\n";
	//
	generatedDetermineNamedScriptIdxToStartDefinition+= definitionIndent + "}" + "\n";
	generatedDetermineNamedScriptIdxToStartDefinition+= "\n";
	generatedDetermineNamedScriptIdxToStartDefinition+= definitionIndent + "//" + "\n";
	generatedDetermineNamedScriptIdxToStartDefinition+= definitionIndent + "return SCRIPTIDX_NONE;" + "\n";
	generatedDetermineNamedScriptIdxToStartDefinition+= definitionIndent + "#undef MINISCRIPT_METHOD_POPSTACK" + "\n";
	generatedDetermineNamedScriptIdxToStartDefinition+= string() + "}" + "\n";

	//
	{
		string emitDefinitionIndent = "\t";
		emitDefinition+= emitDefinitionIndent + "{" + "\n";
		emitDefinition+= emitDefinitionIndent + "\t" + "_Console::printLine(\"" + miniScriptClassName + "::emit(): no condition with name: '\" + condition + \"'\");" + "\n";
		emitDefinition+= emitDefinitionIndent + "}" + "\n";
		emitDefinition+= string() + "}" + "\n";
	}

	//
	if (globalVariables.empty() == false) {
		generatedDeclarations+= declarationIndent + "// global script variables" + "\n";
		for (const auto& variable: globalVariables) {
			generatedDeclarations+= declarationIndent + "Variable " + createGlobalVariableName(variable) + ";" + "\n";
		}
		generatedDeclarations+= "\n";
	}

	//
	{
		auto scriptIdx = 0;
		for (const auto& script: scripts) {
			//
			if (script.type == MiniScript::Script::TYPE_ON ||
				script.type == MiniScript::Script::TYPE_ONENABLED ||
				script.type == MiniScript::Script::TYPE_STACKLET) {
				scriptIdx++;
				continue;
			}
			// method name
			auto shortMethodName = createShortMethodName(miniScript, scriptIdx);
			//
			generatedDeclarations+= declarationIndent + "// local script variables of: " + getScriptTypeReadableName(script.type) + ": " + script.condition + (script.name.empty() == false?" (" + script.name + ")":"") + "\n";
			generatedDeclarations+= declarationIndent + "struct LV_" + shortMethodName + " {" + "\n";
			for (const auto& variable: localVariables[scriptIdx]) {
				generatedDeclarations+= declarationIndent + "\t" + "Variable " + createLocalVariableName(variable) + ";" + "\n";
			}
			generatedDeclarations+= declarationIndent + "\t" + "// destructor, we unset the variable references here" + "\n";
			generatedDeclarations+= declarationIndent + "\t" + "~LV_" + shortMethodName + "() {" + "\n";
			for (const auto& variable: localVariables[scriptIdx]) {
				generatedDeclarations+= declarationIndent + "\t" + "\t" + createLocalVariableName(variable) + ".unset();" + "\n";
			}
			generatedDeclarations+= declarationIndent + "\t" + "}" + "\n";
			generatedDeclarations+= declarationIndent + "};" + "\n";
			generatedDeclarations+= declarationIndent + "stack<LV_" + shortMethodName + "> " + shortMethodName + "_Stack;" + "\n\n";
			//
			scriptIdx++;
		}
	}

	// sum up definitions
	generatedDefinitions =
		string("\n#define __MINISCRIPT_TRANSPILATION__\n\n") +
		initializeNativeDefinition +
		registerMethodsDefinitions +
		generatedDetermineScriptIdxToStartDefinition +
		generatedDetermineNamedScriptIdxToStartDefinition + "\n" +
		emitDefinition +
		generatedDefinitions;

	// inject C++ definition code
	{
		//
		auto injectedGeneratedCode = true;
		//
		vector<string> miniScriptCPP;
		vector<string> generatedMiniScriptCPP;
		auto transpilationCPPFileName = FileSystem::getPathName(transpilationFileName) + "/" + FileSystem::getFileName(transpilationFileName) + ".cpp";
		if (FileSystem::exists(transpilationCPPFileName) == false) {
			auto miniScriptCPPString = FileSystem::getContentAsString("./resources/miniscript/templates/transpilation", "Transpilation.cpp");
			miniScriptCPPString = StringTools::replace(miniScriptCPPString, "{$script}", scriptFileName);
			miniScriptCPPString = StringTools::replace(miniScriptCPPString, "{$class-name}", miniScriptClassName);
			miniScriptCPPString = StringTools::replace(miniScriptCPPString, "{$base-class}", miniScript->getBaseClass());
			miniScriptCPPString = StringTools::replace(miniScriptCPPString, "{$base-class-header}", miniScript->getBaseClassHeader());
			miniScriptCPP = StringTools::tokenize(miniScriptCPPString, "\n", true);
		} else {
			FileSystem::getContentAsStringArray(FileSystem::getPathName(transpilationCPPFileName), FileSystem::getFileName(transpilationCPPFileName), miniScriptCPP);
		}
		//
		if (injectedGeneratedCode == true) {
			//
			string includes;
			for (const auto& include: transpilationUnitIncludes) includes+= include + "\n";
			//
			injectedGeneratedCode = replace(
				miniScriptCPP,
				"/*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_INCLUDES_START__*/",
				"/*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_INCLUDES_END__*/",
				includes,
				generatedMiniScriptCPP
			);
			if (injectedGeneratedCode == false) {
				Console::printLine(scriptFileName + ": Could not inject generated C++ code, are you missing the /*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_INCLUDES_START__*/ and /*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_INCLUDES_END__*/ tags in file " + transpilationFileName + "?");
			} else {
				miniScriptCPP = generatedMiniScriptCPP;
				generatedMiniScriptCPP.clear();
			}
		}
		//
		if (injectedGeneratedCode == true) {
			//
			string usings;
			for (const auto& _using: transpilationUnitUsings) usings+= _using + "\n";
			//
			injectedGeneratedCode = replace(
				miniScriptCPP,
				"/*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_USINGS_START__*/",
				"/*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_USINGS_END__*/",
				usings,
				generatedMiniScriptCPP
			);
			if (injectedGeneratedCode == false) {
				Console::printLine(scriptFileName + ": Could not inject generated C++ code, are you missing the /*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_USINGS_START__*/ and /*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_USINGS_END__*/ tags in file " + transpilationFileName + "?");
			} else {
				miniScriptCPP = generatedMiniScriptCPP;
				generatedMiniScriptCPP.clear();
			}
		}
		//
		if (injectedGeneratedCode == true) {
			injectedGeneratedCode = replace(
				miniScriptCPP,
				"/*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_DEFINITIONS_START__*/",
				"/*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_DEFINITIONS_END__*/",
				generatedDefinitions,
				generatedMiniScriptCPP
			);
			if (injectedGeneratedCode == false) {
				Console::printLine(scriptFileName + ": Could not inject generated C++ code, are you missing the /*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_DEFINITIONS_START__*/ and /*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_DEFINITIONS_END__*/ tags in file " + transpilationFileName + "?");
			} else {
				miniScriptCPP.clear();
			}
		}
		//
		if (injectedGeneratedCode == true) {
			FileSystem::setContentFromStringArray(
				FileSystem::getPathName(transpilationCPPFileName),
				FileSystem::getFileName(transpilationCPPFileName),
				generatedMiniScriptCPP
			);
			//
			Console::printLine(scriptFileName + ": Injected generated C++ code in file " + transpilationCPPFileName + ". Dont forget to rebuild your sources.");
		}
	}

	//
	// inject C++ declaration code / header
	{
		vector<string> miniScriptClassHeader;
		vector<string> generatedMiniScriptClassHeader;
		auto transpilationHeaderFileName = FileSystem::getPathName(transpilationFileName) + "/" + FileSystem::getFileName(transpilationFileName) + ".h";
		if (FileSystem::exists(transpilationHeaderFileName) == false) {
			auto miniScriptHeaderString = FileSystem::getContentAsString("./resources/miniscript/templates/transpilation", "Transpilation.h");
			miniScriptHeaderString = StringTools::replace(miniScriptHeaderString, "{$script}", scriptFileName);
			miniScriptHeaderString = StringTools::replace(miniScriptHeaderString, "{$class-name}", miniScriptClassName);
			miniScriptHeaderString = StringTools::replace(miniScriptHeaderString, "{$base-class}", miniScript->getBaseClass());
			miniScriptHeaderString = StringTools::replace(miniScriptHeaderString, "{$base-class-header}", miniScript->getBaseClassHeader());
			miniScriptClassHeader = StringTools::tokenize(miniScriptHeaderString, "\n", true);
		} else {
			FileSystem::getContentAsStringArray(FileSystem::getPathName(transpilationHeaderFileName), FileSystem::getFileName(transpilationHeaderFileName), miniScriptClassHeader);
		}
		//
		auto injectedGeneratedCode = replace(
			miniScriptClassHeader,
			"/*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_DECLARATIONS_START__*/",
			"/*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_DECLARATIONS_END__*/",
			generatedDeclarations,
			generatedMiniScriptClassHeader
		);
		//
		if (injectedGeneratedCode == false) {
			Console::printLine(scriptFileName + ": Could not inject generated C++ code, are you missing the /*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_DECLARATIONS_START__*/ and /*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_DECLARATIONS_END__*/ tags in file " + transpilationFileName + "?");
		} else {
			//
			FileSystem::setContentFromStringArray(
				FileSystem::getPathName(transpilationHeaderFileName),
				FileSystem::getFileName(transpilationHeaderFileName),
				generatedMiniScriptClassHeader
			);
			//
			Console::printLine(scriptFileName + ": Injected generated C++ code in header file " + transpilationHeaderFileName + ". Dont forget to rebuild your sources.");
		}
	}
}

void Transpiler::untranspile(const string& scriptFileName, const string& transpilationFileName) {
	Console::printLine(scriptFileName + ": Processing script");
	//
	auto replace = [](const vector<string> input, const string& startTag, const string& endTag, const string& replacement, vector<string>& output) -> bool {
		auto reject = false;
		auto replaceSuccess = false;
		for (auto i = 0; i < input.size(); i++) {
			const auto& line = input[i];
			auto trimmedLine = StringTools::trim(line);
			if (StringTools::startsWith(trimmedLine, "//") == true) {
				if (reject == false) output.push_back(line);
				continue;
			}
			if (trimmedLine == startTag) {
				reject = true;
				output.push_back(line);
			} else
			if (trimmedLine == endTag) {
				reject = false;
				replaceSuccess = true;
				output.push_back(replacement);
				output.push_back(line);
			} else {
				if (reject == false) output.push_back(line);
			}
		}
		//
		return replaceSuccess;
	};
	// de-inject C++ definition code
	{
		vector<string> miniScriptClass;
		vector<string> miniScriptClassNew;
		auto transpilationCPPFileName = FileSystem::getPathName(transpilationFileName) + "/" + FileSystem::getFileName(transpilationFileName) + ".cpp";
		FileSystem::getContentAsStringArray(FileSystem::getPathName(transpilationCPPFileName), FileSystem::getFileName(transpilationCPPFileName), miniScriptClass);
		//
		auto injectedGeneratedCode = false;
		{
			injectedGeneratedCode = replace(
				miniScriptClass,
				"/*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_INCLUDES_START__*/",
				"/*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_INCLUDES_END__*/",
				string(),
				miniScriptClassNew
			);
			miniScriptClass = miniScriptClassNew;
			miniScriptClassNew.clear();
		}
		if (injectedGeneratedCode == true) {
			injectedGeneratedCode = replace(
				miniScriptClass,
				"/*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_USINGS_START__*/",
				"/*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_USINGS_END__*/",
				string(),
				miniScriptClassNew
			);
			miniScriptClass = miniScriptClassNew;
			miniScriptClassNew.clear();
		}
		if (injectedGeneratedCode == true) {
			injectedGeneratedCode = replace(
				miniScriptClass,
				"/*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_DEFINITIONS_START__*/",
				"/*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_DEFINITIONS_END__*/",
				string(),
				miniScriptClassNew
			);
		}
		//
		if (injectedGeneratedCode == false) {
			Console::printLine(scriptFileName + ": Could not remove generated C++ code, are you missing the /*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_DEFINITIONS_START__*/ and /*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_DEFINITIONS_END__*/ markers in file " + transpilationFileName + "?");
		} else {
			//
			FileSystem::setContentFromStringArray(
				FileSystem::getPathName(transpilationCPPFileName),
				FileSystem::getFileName(transpilationCPPFileName),
				miniScriptClassNew
			);
			//
			Console::printLine(scriptFileName + ": Removed generated C++ code in file " + transpilationCPPFileName + ". Dont forget to rebuild your sources.");
		}
	}
	//
	// inject C++ declaration code / header
	{
		vector<string> miniScriptClassHeader;
		vector<string> miniScriptClassHeaderNew;
		auto transpilationHeaderFileName = FileSystem::getPathName(transpilationFileName) + "/" + FileSystem::getFileName(transpilationFileName) + ".h";
		FileSystem::getContentAsStringArray(FileSystem::getPathName(transpilationHeaderFileName), FileSystem::getFileName(transpilationHeaderFileName), miniScriptClassHeader);
		//
		auto injectedGeneratedCode = false;
		injectedGeneratedCode = replace(
			miniScriptClassHeader,
			"/*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_DECLARATIONS_START__*/",
			"/*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_DECLARATIONS_END__*/",
			string(),
			miniScriptClassHeaderNew
		);
		//
		if (injectedGeneratedCode == false) {
			Console::printLine(scriptFileName + ": Could not remove generated C++ code, are you missing the /*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_DECLARATIONS_START__*/ and /*__MINISCRIPT_TRANSPILEDMINISCRIPTCODE_DECLARATIONS_END__*/ markers in file " + transpilationFileName + "?");
		} else {
			//
			FileSystem::setContentFromStringArray(
				FileSystem::getPathName(transpilationHeaderFileName),
				FileSystem::getFileName(transpilationHeaderFileName),
				miniScriptClassHeaderNew
			);
			//
			Console::printLine(scriptFileName + ": Removed generated C++ code in header file " + transpilationHeaderFileName + ". Dont forget to rebuild your sources.");
		}
	}
}

const unordered_set<string> Transpiler::getAllClassesMethodNames(MiniScript* miniScript) {
	unordered_set<string> allMethods;
	for (auto scriptMethod: miniScript->getMethods()) {
		string className;
		if (scriptMethod->getMethodName().rfind("::") != string::npos) className = StringTools::substring(scriptMethod->getMethodName(), 0, scriptMethod->getMethodName().rfind("::"));
		if (className.empty() == true) continue;
		string method =
			StringTools::substring(
				scriptMethod->getMethodName(),
				className.size() + 2,
				scriptMethod->getMethodName().size());
		// first argument name of method must equal the name of the class
		if (scriptMethod->getArgumentTypes().empty() == true ||
			scriptMethod->getArgumentTypes()[0].name != StringTools::toLowerCase(className)) continue;
		// first argument of method must be of type of the class
		if (className != MiniScript::Variable::getTypeAsString(scriptMethod->getArgumentTypes()[0].type)) continue;		//
		allMethods.insert(method);
	}
	//
	return allMethods;
}

const vector<string> Transpiler::getAllClassesMethodNamesSorted(MiniScript* miniScript) {
	auto allMethods = getAllClassesMethodNames(miniScript);
	//
	vector<string> result;
	for (auto method: allMethods) result.push_back(method);
	sort(result.begin(), result.end());
	//
	return result;
}

const unordered_map<string, vector<string>> Transpiler::getClassesMethodNames(MiniScript* miniScript) {
	unordered_map<string, vector<string>> methodByClasses;
	for (auto scriptMethod: miniScript->getMethods()) {
		string className;
		if (scriptMethod->getMethodName().rfind("::") != string::npos) className = StringTools::substring(scriptMethod->getMethodName(), 0, scriptMethod->getMethodName().rfind("::"));
		if (className.empty() == true) continue;
		string method =
			StringTools::substring(
				scriptMethod->getMethodName(),
				className.size() + 2,
				scriptMethod->getMethodName().size());
		// first argument name of method must equal the name of the class
		if (scriptMethod->getArgumentTypes().empty() == true ||
			scriptMethod->getArgumentTypes()[0].name != StringTools::toLowerCase(className)) continue;
		// first argument of method must be of type of the class
		if (className != MiniScript::Variable::getTypeAsString(scriptMethod->getArgumentTypes()[0].type)) continue;
		//
		methodByClasses[className].push_back(method);
	}
	//
	return methodByClasses;
}

void Transpiler::determineVariables(MiniScript* miniScript, unordered_set<string>& globalVariables, vector<unordered_set<string>>& localVariables) {
	// TODO:
	//	Problem: Variables can be set before read  or read before set
	//		we allow to read from GLOBAL variable also if local variable has not been found
	//		One fix would be to: Dont look variable up in parent context, but rather also need a $GLOBAL accessor
	//
	const auto& scripts = miniScript->getScripts();
	//
	{
		auto scriptIdx = 0;
		for (const auto& script: scripts) {
			//
			determineVariables(MiniScript::SCRIPTIDX_NONE, script.conditionSyntaxTree, globalVariables, localVariables);
			//
			for (auto statementIdx = 0; statementIdx < script.statements.size(); statementIdx++) {
				determineVariables(
					script.type == MiniScript::Script::TYPE_FUNCTION || script.type == MiniScript::Script::TYPE_STACKLET?
						scriptIdx:
						MiniScript::SCRIPTIDX_NONE,
					script.syntaxTree[statementIdx],
					globalVariables,
					localVariables
				);
			}
			//
			scriptIdx++;
		}
	}
	// move stacklet variables into their scopes
	for (auto scriptIdx = 0; scriptIdx < localVariables.size(); scriptIdx++) {
		const auto& script = scripts[scriptIdx];
		if (script.type != MiniScript::Script::TYPE_STACKLET) continue;
		//
		auto stackletScopeScriptIdx = miniScript->getStackletScopeScriptIdx(scriptIdx);
		for (const auto& variable: localVariables[scriptIdx]) {
			if (stackletScopeScriptIdx == MiniScript::SCRIPTIDX_NONE) {
				globalVariables.insert(variable);
			} else {
				localVariables[stackletScopeScriptIdx].insert(variable);
			}
		}
		localVariables[scriptIdx].clear();
	}
}

void Transpiler::determineVariables(int scriptIdx, const MiniScript::SyntaxTreeNode& syntaxTreeNode, unordered_set<string>& globalVariables, vector<unordered_set<string>>& localVariables) {
	auto addVariable = [&](const string& variableStatement) -> void {
		//
		if (scriptIdx == MiniScript::SCRIPTIDX_NONE ||
			StringTools::startsWith(variableStatement, "$$.") == true ||
			StringTools::startsWith(variableStatement, "$GLOBAL.") == true) {
			//
			if (StringTools::startsWith(variableStatement, "$$.") == true) {
				const auto variableName = createVariableName("$" + StringTools::substring(variableStatement, string_view("$$.").size()));
				globalVariables.insert(variableName);
			} else
			if (StringTools::startsWith(variableStatement, "$GLOBAL.") == true) {
				const auto variableName = createVariableName("$" + StringTools::substring(variableStatement, string_view("$GLOBAL.").size()));
				globalVariables.insert(variableName);
			} else {
				const auto variableName = createVariableName(variableStatement);
				globalVariables.insert(variableName);
			}
		} else {
			const auto variableName = createVariableName(variableStatement);
			localVariables[scriptIdx].insert(variableName);
		}
	};
	//
	switch (syntaxTreeNode.type) {
		case MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_LITERAL:
			{
				break;
			}
		case MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_EXECUTE_FUNCTION:
			{
				for (const auto& argument: syntaxTreeNode.arguments) determineVariables(scriptIdx, argument, globalVariables, localVariables);
				//
				break;
			}
		case MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_EXECUTE_METHOD:
			{
				if ((syntaxTreeNode.value.getValueAsString() == "getMethodArgumentVariable" ||
					syntaxTreeNode.value.getValueAsString() == "getVariable" ||
					syntaxTreeNode.value.getValueAsString() == "getVariableReference" ||
					syntaxTreeNode.value.getValueAsString() == "setVariable" ||
					syntaxTreeNode.value.getValueAsString() == "setVariableReference" ||
					syntaxTreeNode.value.getValueAsString() == "setConstant" ||
					syntaxTreeNode.value.getValueAsString() == "unsetVariable") &&
					syntaxTreeNode.arguments.empty() == false &&
					syntaxTreeNode.arguments[0].type == MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_LITERAL) {
					//
					addVariable(syntaxTreeNode.arguments[0].value.getValueAsString());
				} else
				if (syntaxTreeNode.value.getValueAsString() == "internal.script.evaluateMemberAccess") {
					if (syntaxTreeNode.arguments[0].value.getType() == MiniScript::TYPE_STRING) {
						addVariable(syntaxTreeNode.arguments[0].value.getValueAsString());
					}
					for (auto i = 3; i < syntaxTreeNode.arguments.size(); i+= 2) {
						if (syntaxTreeNode.arguments[i].value.getType() == MiniScript::TYPE_STRING) {
							addVariable(syntaxTreeNode.arguments[i].value.getValueAsString());
						}
					}
				}
				//
				for (const auto& argument: syntaxTreeNode.arguments) determineVariables(scriptIdx, argument, globalVariables, localVariables);
				//
				break;
			}
		default:
			break;
	}
}

void Transpiler::gatherMethodCode(
	const vector<string>& miniScriptExtensionsCode,
	const string& className,
	int registerLine,
	unordered_map<string, vector<string>>& methodCodeMap
) {
	// TODO: this is a bit ugly and can be improved a lot, lets see and get this to work first
	auto classDefinitionLine = -1;
	// get class definition start line
	for (auto i = registerLine; i >= 0; i--) {
		const auto& line = miniScriptExtensionsCode[i];
		auto trimmedLine = StringTools::trim(line);
		if (StringTools::regexMatch(trimmedLine, "class[\\ \\t]+" + className + "[\\ \\t]*:.*") == true) {
			classDefinitionLine = i;
			break;
		}
	}
	// nope
	if (classDefinitionLine == -1) {
		Console::printLine("Transpiler::gatherMethodCode(): did not found '" + className + "' definition");
		return;
	}
	//
	auto curlyBracketCount = 0;
	auto finished = false;
	auto haveExecuteMethodDeclaration = false;
	auto executeMethodCurlyBracketStart = -1;
	auto haveGetMethodNameDeclaration = false;
	auto getMethodNameCurlyBracketStart = -1;
	vector<string> executeMethodCode;
	string getMethodNameCode;
	for (auto i = classDefinitionLine; finished == false && i < miniScriptExtensionsCode.size(); i++) {
		const auto& line = miniScriptExtensionsCode[i];
		auto trimmedLine = StringTools::trim(line);
		// have getMethodName declaration, with following body
		if (StringTools::regexMatch(trimmedLine, "const[\\ \\t]+string[\\ \\t]+getMethodName()[\\ \\t]*\\(.*") == true) {
			haveGetMethodNameDeclaration = true;
		}
		// have executeMethod declaration, with following body
		if (StringTools::regexMatch(trimmedLine, "void[\\ \\t]+executeMethod[\\ \\t]*\\(.*") == true) {
			haveExecuteMethodDeclaration = true;
		}
		//
		for (auto j = 0; j < trimmedLine.size(); j++) {
			auto c = trimmedLine[j];
			if (c == '{') {
				curlyBracketCount++;
				// new code block,
				// 	if we have the declaration mark this curly bracket as executeMethod implementation start curly bracket
				if (haveExecuteMethodDeclaration == true) {
					executeMethodCurlyBracketStart = curlyBracketCount;
				}
				// 	if we have the declaration mark this curly bracket as getMethodName implementation start curly bracket
				if (haveGetMethodNameDeclaration == true) {
					getMethodNameCurlyBracketStart = curlyBracketCount;
				}
			} else
			if (c == '}') {
				// do we just leave our getMethodName implementation?
				if (getMethodNameCurlyBracketStart != -1 && curlyBracketCount == getMethodNameCurlyBracketStart) {
					// yup
					getMethodNameCurlyBracketStart = -1;
				}
				// do we just leave our executeMethod implementation?
				if (executeMethodCurlyBracketStart != -1 && curlyBracketCount == executeMethodCurlyBracketStart) {
					// yup
					executeMethodCurlyBracketStart = -1;
				}
				//
				curlyBracketCount--;
				// get out of here :D
				if (curlyBracketCount <= 0) {
					finished = true;
					break;
				}
			}
		}
		// is this getMethodName code?
		if (haveGetMethodNameDeclaration == false && getMethodNameCurlyBracketStart != -1 && curlyBracketCount >= getMethodNameCurlyBracketStart) {
			getMethodNameCode+= trimmedLine;
		}
		// is this executeMethod code?
		if (haveExecuteMethodDeclaration == false && executeMethodCurlyBracketStart != -1 && curlyBracketCount >= executeMethodCurlyBracketStart) {
			executeMethodCode.push_back(line);
		}
		// do we still process getMethodName declaration
		if (haveGetMethodNameDeclaration == true) {
			// ok unset
			haveGetMethodNameDeclaration = false;
		}
		// do we still process executeMethod declaration
		if (haveExecuteMethodDeclaration == true) {
			// ok unset
			haveExecuteMethodDeclaration = false;
		}
	}

	// determine method name
	string methodName;
	{
		auto haveMethodName = false;
		for (auto i = 0; i < getMethodNameCode.size(); i++) {
			auto c = getMethodNameCode[i];
			if (c == '"') {
				if (haveMethodName == false) haveMethodName = true; else
					break;
			} else
			if (haveMethodName == true) {
				methodName+= c;
			}
		}
	}

	// find min indent from method code and depth indent
	int minIndent = Integer::MAX_VALUE;
	for (const auto& codeLine: executeMethodCode) {
		auto indent = 0;
		for (auto i = 0; i < codeLine.size(); i++) {
			auto c = codeLine[i];
			if (c == '\t') {
				indent++;
			} else {
				break;
			}
		}
		minIndent = Math::min(minIndent, indent);
	}

	// remove indent
	for (auto& codeLine: executeMethodCode) {
		codeLine = StringTools::substring(codeLine, minIndent);
	}

	//
	auto methodCodeMapIt = methodCodeMap.find(methodName);
	if (methodCodeMapIt != methodCodeMap.end()) {
		Console::printLine("Transpiler::gatherMethodCode(): Not registering method with methodName: '" + methodName + "': method already registered");
		return;
	}

	//
	Console::printLine("Transpiler::gatherMethodCode(): registering method with methodName: '" + methodName + "'");

	//
	methodCodeMap[methodName] = executeMethodCode;
}

void Transpiler::generateVariableAccess(
	MiniScript* miniScript,
	string& generatedCode,
	int scriptConditionIdx,
	int scriptIdx,
	const string& variableName,
	const string& indent,
	bool getMethodArgumentVariable,
	bool getVariable,
	bool getVariableReference,
	bool setVariable,
	bool setVariableReference,
	bool setConstant,
	const string& returnValueStatement,
	const string& statementEnd,
	int getArgumentIdx,
	int setArgumentIdx
) {
	//
	auto haveFunction = false;
	auto haveScript = (scriptConditionIdx != MiniScript::SCRIPTIDX_NONE || scriptIdx != MiniScript::SCRIPTIDX_NONE);
	if (haveScript == true) {
		const auto& script = miniScript->getScripts()[scriptConditionIdx != MiniScript::SCRIPTIDX_NONE?scriptConditionIdx:scriptIdx];
		haveFunction =
			script.type == MiniScript::Script::TYPE_FUNCTION ||
			(script.type == MiniScript::Script::TYPE_STACKLET && miniScript->getStackletScopeScriptIdx(scriptIdx) != MiniScript::SCRIPTIDX_NONE);
	}
	//
	auto dollarDollarVariable = StringTools::startsWith(variableName, "$$.");
	auto dollarGlobalVariable = StringTools::startsWith(variableName, "$GLOBAL.");
	if (haveFunction == true ||
		dollarDollarVariable == true ||
		dollarGlobalVariable == true) {
		//
		if (dollarDollarVariable == true || dollarGlobalVariable == true) {
			auto globalVariableIdx = 0;
			string globalVariable;
			if (dollarDollarVariable == true) {
				globalVariable = "$" + StringTools::substring(variableName, string_view("$$.").size());
				globalVariableIdx = 3;
			}
			if (dollarGlobalVariable == true) {
				globalVariable = "$" + StringTools::substring(variableName, string_view("$GLOBAL.").size());
				globalVariableIdx = 8;
			}
			auto haveVariableStatement = variableHasStatement(globalVariable);
			if (getMethodArgumentVariable == true) {
				if (haveVariableStatement == true) {
					generatedCode+= indent + returnValueStatement + "getMethodArgumentVariable(&" + createGlobalVariableName(globalVariable) + ", \"$\" + StringTools::substring(arguments[" + to_string(getArgumentIdx) + "].getValueAsString(), " + to_string(globalVariableIdx) + "), &statement)" + statementEnd;
				} else {
					generatedCode+= indent + returnValueStatement + "Variable::createMethodArgumentVariable(&" + createGlobalVariableName(globalVariable) + ")" + statementEnd;
				}
			} else
			if (getVariable == true) {
				if (haveVariableStatement == true) {
					generatedCode+= indent + returnValueStatement + "getVariable(&" + createGlobalVariableName(globalVariable) + ", \"$\" + StringTools::substring(arguments[" + to_string(getArgumentIdx) + "].getValueAsString(), " + to_string(globalVariableIdx) + "), &statement, false)" + statementEnd;
				} else {
					generatedCode+= indent + returnValueStatement + "Variable::createNonReferenceVariable(&" + createGlobalVariableName(globalVariable) + ")" + statementEnd;
				}
			} else
			if (getVariableReference == true) {
				if (haveVariableStatement == true) {
					generatedCode+= indent + returnValueStatement + "getVariable(&" + createGlobalVariableName(globalVariable) + ", \"$\" + StringTools::substring(arguments[" + to_string(getArgumentIdx) + "].getValueAsString(), " + to_string(globalVariableIdx) + "), &statement, true)" + statementEnd;
				} else {
					generatedCode+= indent + returnValueStatement + "Variable::createReferenceVariable(&" + createGlobalVariableName(globalVariable) + ")" + statementEnd;
				}
			} else
			if (setVariable == true || setConstant == true) {
				if (haveVariableStatement == true) {
					if (setConstant == true) {
						generatedCode+= indent + "setConstant(&" + createGlobalVariableName(globalVariable) + ", \"$\" + StringTools::substring(arguments[" + to_string(getArgumentIdx) + "].getValueAsString(), " + to_string(globalVariableIdx) + "), arguments[" + to_string(setArgumentIdx) + "], &statement); returnValue = arguments[" + to_string(setArgumentIdx) + "]" + statementEnd;
					} else {
						generatedCode+= indent + "setVariable(&" + createGlobalVariableName(globalVariable) + ", \"$\" + StringTools::substring(arguments[" + to_string(getArgumentIdx) + "].getValueAsString(), " + to_string(globalVariableIdx) + "), arguments[" + to_string(setArgumentIdx) + "].isConstant() == true?MiniScript::Variable::createNonConstVariable(&arguments[" + to_string(setArgumentIdx) + "]):arguments[" + to_string(setArgumentIdx) + "], &statement); returnValue = arguments[" + to_string(setArgumentIdx) + "]" + statementEnd;
					}
				} else {
					generatedCode+= indent + "if (" + createGlobalVariableName(globalVariable) + ".isConstant() == true) _Console::printLine(getStatementInformation(statement) + \": constant: Assignment of constant is not allowed\"); else ";
					if (setConstant == true) {
						generatedCode+= "{ ";
						generatedCode+= createGlobalVariableName(globalVariable) + ".setValue(arguments[" + to_string(setArgumentIdx) + "]); ";
						generatedCode+= "setConstant(" + createGlobalVariableName(globalVariable) + "); ";
						generatedCode+= "} ";
					} else {
						generatedCode+= createGlobalVariableName(globalVariable) + ".setValue(arguments[" + to_string(setArgumentIdx) + "].isConstant() == true?MiniScript::Variable::createNonConstVariable(&arguments[" + to_string(setArgumentIdx) + "]):arguments[" + to_string(setArgumentIdx) + "]); ";
					}
					generatedCode+= returnValueStatement + "arguments[" + to_string(setArgumentIdx) + "]" + statementEnd;
				}
			} else
			if (setVariableReference == true) {
				if (haveVariableStatement == true) {
					generatedCode+= indent + "setVariable(&" + createGlobalVariableName(globalVariable) + ", \"$\" + StringTools::substring(arguments[" + to_string(getArgumentIdx) + "].getValueAsString(), " + to_string(globalVariableIdx) + "), arguments[" + to_string(setArgumentIdx) + "], &statement, true); returnValue = arguments[" + to_string(setArgumentIdx) + "]" + statementEnd;
				} else {
					generatedCode+= indent + "if (" + createGlobalVariableName(globalVariable) + ".isConstant() == true) _Console::printLine(getStatementInformation(statement) + \": constant: Assignment of constant is not allowed\"); else ";
					generatedCode+= createGlobalVariableName(globalVariable) + ".setReference(&arguments[" + to_string(setArgumentIdx) + "]); " + returnValueStatement + "arguments[" + to_string(setArgumentIdx) + "]" + statementEnd;
				}
			}
		} else {
			const auto& localVariable = variableName;
			auto haveVariableStatement = variableHasStatement(localVariable);
			if (getMethodArgumentVariable == true) {
				if (haveVariableStatement == true) {
					generatedCode+= indent + returnValueStatement + "getMethodArgumentVariable(&_lv." + createLocalVariableName(localVariable) + ", arguments[" + to_string(getArgumentIdx) + "].getValueAsString(), &statement)" + statementEnd;
				} else {
					generatedCode+= indent + returnValueStatement + "Variable::createMethodArgumentVariable(&_lv." + createLocalVariableName(localVariable) + ")" + statementEnd;
				}
			} else
			if (getVariable == true) {
				if (haveVariableStatement == true) {
					generatedCode+= indent + returnValueStatement + "getVariable(&_lv." + createLocalVariableName(localVariable) + ", arguments[" + to_string(getArgumentIdx) + "].getValueAsString(), &statement, false)" + statementEnd;
				} else {
					generatedCode+= indent + returnValueStatement + "Variable::createNonReferenceVariable(&_lv." + createLocalVariableName(localVariable) + ")" + statementEnd;
				}
			} else
			if (getVariableReference == true) {
				if (haveVariableStatement == true) {
					generatedCode+= indent + returnValueStatement + "getVariable(&_lv." + createLocalVariableName(localVariable) + ", arguments[" + to_string(getArgumentIdx) + "].getValueAsString(), &statement, true)" + statementEnd;
				} else {
					generatedCode+= indent + returnValueStatement + "Variable::createReferenceVariable(&_lv." + createLocalVariableName(localVariable) + ")" + statementEnd;
				}
			} else
			if (setVariable == true || setConstant == true) {
				if (haveVariableStatement == true) {
					if (setConstant == true) {
						generatedCode+= indent + "setConstant(&_lv." + createLocalVariableName(localVariable) + ", arguments[" + to_string(getArgumentIdx) + "].getValueAsString(), arguments[" + to_string(setArgumentIdx) + "], &statement); returnValue = arguments[" + to_string(setArgumentIdx) + "]" + statementEnd;
					} else {
						generatedCode+= indent + "setVariable(&_lv." + createLocalVariableName(localVariable) + ", arguments[" + to_string(getArgumentIdx) + "].getValueAsString(), arguments[" + to_string(setArgumentIdx) + "].isConstant() == true?MiniScript::Variable::createNonConstVariable(&arguments[" + to_string(setArgumentIdx) + "]):arguments[" + to_string(setArgumentIdx) + "], &statement); returnValue = arguments[" + to_string(setArgumentIdx) + "]" + statementEnd;
					}
				} else {
					generatedCode+= indent + "if (_lv." + createLocalVariableName(localVariable) + ".isConstant() == true) _Console::printLine(getStatementInformation(statement) + \": constant: Assignment of constant is not allowed\"); else ";
					if (setConstant == true) {
						generatedCode+= "{ ";
						generatedCode+= "_lv." + createLocalVariableName(localVariable) + ".setValue(arguments[" + to_string(setArgumentIdx) + "]); ";
						generatedCode+= "setConstant(_lv." + createLocalVariableName(localVariable) + "); ";
						generatedCode+= "} ";
					} else {
						generatedCode+= "_lv." + createLocalVariableName(localVariable) + ".setValue(arguments[" + to_string(setArgumentIdx) + "].isConstant() == true?MiniScript::Variable::createNonConstVariable(&arguments[" + to_string(setArgumentIdx) + "]):arguments[" + to_string(setArgumentIdx) + "]); ";
					}
					generatedCode+= returnValueStatement + "arguments[" + to_string(setArgumentIdx) + "]" + statementEnd;
				}
			} else
			if (setVariableReference == true) {
				if (haveVariableStatement == true) {
					generatedCode+= indent + "setVariable(&_lv." + createLocalVariableName(localVariable) + ", arguments[" + to_string(getArgumentIdx) + "].getValueAsString(), arguments[" + to_string(setArgumentIdx) + "], &statement, true); returnValue = arguments[" + to_string(setArgumentIdx) + "]" + statementEnd;
				} else {
					generatedCode+= indent + "if (_lv." + createLocalVariableName(localVariable) + ".isConstant() == true) _Console::printLine(getStatementInformation(statement) + \": constant: Assignment of constant is not allowed\"); else ";
					generatedCode+= "_lv." + createLocalVariableName(localVariable) + ".setReference(&arguments[" + to_string(setArgumentIdx) + "]); " + returnValueStatement + "arguments[" + to_string(setArgumentIdx) + "]" + statementEnd;
				}
			}
		}
	} else {
		//
		const auto& globalVariable = variableName;
		auto haveVariableStatement = variableHasStatement(globalVariable);
		if (getMethodArgumentVariable == true) {
			if (haveVariableStatement == true) {
				generatedCode+= indent + returnValueStatement + "getMethodArgumentVariable(&" + createGlobalVariableName(globalVariable) + ", arguments[" + to_string(getArgumentIdx) + "].getValueAsString(), &statement)" + statementEnd;
			} else {
				generatedCode+= indent + returnValueStatement + "Variable::createMethodArgumentVariable(&" + createGlobalVariableName(globalVariable) + ")" + statementEnd;
			}
		} else
		if (getVariable == true) {
			if (haveVariableStatement == true) {
				generatedCode+= indent + returnValueStatement + "getVariable(&" + createGlobalVariableName(globalVariable) + ", arguments[" + to_string(getArgumentIdx) + "].getValueAsString(), &statement, false)" + statementEnd;
			} else {
				generatedCode+= indent + returnValueStatement + "Variable::createNonReferenceVariable(&" + createGlobalVariableName(globalVariable) + ")" + statementEnd;
			}
		} else
		if (getVariableReference == true) {
			if (haveVariableStatement == true) {
				generatedCode+= indent + returnValueStatement + "getVariable(&" + createGlobalVariableName(globalVariable) + ", arguments[" + to_string(getArgumentIdx) + "].getValueAsString(), &statement, true)" + statementEnd;
			} else {
				generatedCode+= indent + returnValueStatement + "Variable::createReferenceVariable(&" + createGlobalVariableName(globalVariable) + ")" + statementEnd;
			}
		} else
		if (setVariable == true || setConstant == true) {
			if (haveVariableStatement == true) {
				if (setConstant == true) {
					generatedCode+= indent + "setConstant(&" + createGlobalVariableName(globalVariable) + ", arguments[" + to_string(getArgumentIdx) + "].getValueAsString(), arguments[" + to_string(setArgumentIdx) + "], &statement); " + returnValueStatement + "arguments[" + to_string(getArgumentIdx) + "]" + statementEnd;
				} else {
					generatedCode+= indent + "setVariable(&" + createGlobalVariableName(globalVariable) + ", arguments[" + to_string(getArgumentIdx) + "].getValueAsString(), arguments[" + to_string(setArgumentIdx) + "].isConstant() == true?MiniScript::Variable::createNonConstVariable(&arguments[" + to_string(setArgumentIdx) + "]):arguments[" + to_string(setArgumentIdx) + "], &statement); " + returnValueStatement + "arguments[" + to_string(getArgumentIdx) + "]" + statementEnd;
				}
			} else {
				generatedCode+= indent + "if (" + createGlobalVariableName(globalVariable) + ".isConstant() == true) _Console::printLine(getStatementInformation(statement) + \": constant: Assignment of constant is not allowed\"); else ";
				if (setConstant == true) {
					generatedCode+= "{ ";
					generatedCode+= createGlobalVariableName(globalVariable) + ".setValue(arguments[" + to_string(setArgumentIdx) + "]); ";
					generatedCode+= "setConstant(" + createGlobalVariableName(globalVariable) + "); ";
					generatedCode+= "} ";
				} else {
					generatedCode+= createGlobalVariableName(globalVariable) + ".setValue(arguments[" + to_string(setArgumentIdx) + "].isConstant() == true?MiniScript::Variable::createNonConstVariable(&arguments[" + to_string(setArgumentIdx) + "]):arguments[" + to_string(setArgumentIdx) + "]); ";
				}
				generatedCode+= returnValueStatement + "arguments[" + to_string(setArgumentIdx) + "]" + statementEnd;
			}
		} else
		if (setVariableReference == true) {
			if (haveVariableStatement == true) {
				generatedCode+= indent + "setVariable(&" + createGlobalVariableName(globalVariable) + ", arguments[" + to_string(getArgumentIdx) + "].getValueAsString(), arguments[" + to_string(setArgumentIdx) + "], &statement, true); " + returnValueStatement + "arguments[" + to_string(getArgumentIdx) + "]" + statementEnd;
			} else {
				generatedCode+= indent + "if (" + createGlobalVariableName(globalVariable) + ".isConstant() == true) _Console::printLine(getStatementInformation(statement) + \": constant: Assignment of constant is not allowed\"); else ";
				generatedCode+= createGlobalVariableName(globalVariable) + ".setReference(&arguments[" + to_string(setArgumentIdx) + "]); " + returnValueStatement + "arguments[" + to_string(setArgumentIdx) + "]" + statementEnd;
			}
		}
	}
}

void Transpiler::generateArrayAccessMethods(
	MiniScript* miniScript,
	string& generatedDefinitions,
	const string& miniScriptClassName,
	int scriptConditionIdx,
	int scriptIdx,
	const string& methodName,
	const MiniScript::SyntaxTreeNode& syntaxTree,
	const MiniScript::Statement& statement,
	const unordered_map<string, vector<string>>& methodCodeMap,
	const unordered_set<string>& allMethods,
	bool condition,
	const vector<int>& argumentIndices,
	int depth
	) {
	//
	switch (syntaxTree.type) {
		case MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_LITERAL:
			{
				break;
			}
		case MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_EXECUTE_METHOD:
			{
				if (syntaxTree.value.getValueAsString() == "getMethodArgumentVariable" ||
					syntaxTree.value.getValueAsString() == "getVariable" ||
					syntaxTree.value.getValueAsString() == "getVariableReference" ||
					syntaxTree.value.getValueAsString() == "setVariable" ||
					syntaxTree.value.getValueAsString() == "setVariableReference" ||
					syntaxTree.value.getValueAsString() == "setConstant") {
					//
					auto lamdaIndent = StringTools::indent(string(), "\t", depth);
					//
					for (auto argumentIdx = 0; argumentIdx < syntaxTree.arguments.size(); argumentIdx++) {
						auto argumentString = StringTools::replace(StringTools::replace(syntaxTree.arguments[argumentIdx].value.getValueAsString(), "\\", "\\\\"), "\"", "\\\"");
						//
						auto nextArgumentIndices = argumentIndices;
						nextArgumentIndices.push_back(argumentIdx);
						// ignore array and map initializers
						if (StringTools::startsWith(argumentString, "[") == true ||
							StringTools::startsWith(argumentString, "{") == true) continue;
						//
						auto arrayAccessStatementIdx = 0;
						auto arrayAccessStatementLeftIdx = -1;
						auto arrayAccessStatementRightIdx = -1;
						auto quote = '\0';
						auto bracketCount = 0;
						for (auto i = 0; i < argumentString.size(); i++) {
							auto c = argumentString[i];
							// handle quotes
							if (quote != '\0') {
								// unset quote if closed
								// also we can ignore content of quote blocks
								if (c == quote) {
									quote = '\0';
								}
							} else
							if (c == '"' || c == '\'') {
								quote = c;
							} else
							if (c == '[') {
								if (bracketCount == 0) arrayAccessStatementLeftIdx = i;
								bracketCount++;
							} else
							if (c == ']') {
								bracketCount--;
								if (bracketCount == 0) {
									arrayAccessStatementRightIdx = i;
									//
									auto arrayAccessStatementString = StringTools::substring(argumentString, arrayAccessStatementLeftIdx + 1, arrayAccessStatementRightIdx);
									// array append operator []
									if (arrayAccessStatementString.empty() == true) {
										//
										arrayAccessStatementIdx++;
										//
										continue;
									}
									// check if literal
									MiniScript::Variable arrayAccessStatementAsScriptVariable;
									//
									arrayAccessStatementAsScriptVariable.setImplicitTypedValue(arrayAccessStatementString, miniScript, scriptIdx, statement);
									switch (arrayAccessStatementAsScriptVariable.getType()) {
										case MiniScript::TYPE_BOOLEAN:
											{
												bool booleanValue;
												if (arrayAccessStatementAsScriptVariable.getBooleanValue(booleanValue) == true) {
													generatedDefinitions+= lamdaIndent + "// Miniscript transpilation for a " + (condition == true?"condition":"statement") + " array access statement, statement index " + to_string(statement.statementIdx) + ", argument indices " + MiniScript::getArgumentIndicesAsString(nextArgumentIndices, ", ") + ", array access statement index " + to_string(arrayAccessStatementIdx) + "\n";
													generatedDefinitions+= lamdaIndent + "auto array_access_statement_" + to_string(statement.statementIdx) + "_" + MiniScript::getArgumentIndicesAsString(nextArgumentIndices, "_") + "_" + to_string(arrayAccessStatementIdx) + " = [&](const Statement& statement) -> Variable {" + "\n";
													generatedDefinitions+= lamdaIndent + "	return Variable(" + (booleanValue == true?"true":"false") + ");" + "\n";
													generatedDefinitions+= lamdaIndent + "};" + "\n";
												}
												// literals
												arrayAccessStatementIdx++;
												//
												continue;
											}
										case MiniScript::TYPE_INTEGER:
											{
												int64_t integerValue;
												if (arrayAccessStatementAsScriptVariable.getIntegerValue(integerValue) == true) {
													generatedDefinitions+= lamdaIndent + "// Miniscript transpilation for a " + (condition == true?"condition":"statement") + " array access statement, statement index " + to_string(statement.statementIdx) + ", argument indices " + MiniScript::getArgumentIndicesAsString(nextArgumentIndices, ", ") + ", array access statement index " + to_string(arrayAccessStatementIdx) + "\n";
													generatedDefinitions+= lamdaIndent + "auto array_access_statement_" + to_string(statement.statementIdx) + "_" + MiniScript::getArgumentIndicesAsString(nextArgumentIndices, "_") + "_" + to_string(arrayAccessStatementIdx) + " = [&](const Statement& statement) -> Variable {" + "\n";
													generatedDefinitions+= lamdaIndent + "	return Variable(static_cast<int64_t>(" + to_string(integerValue) + "ll));" + "\n";
													generatedDefinitions+= lamdaIndent + "};" + "\n";
												}
												// literals
												arrayAccessStatementIdx++;
												//
												continue;
											}
										case MiniScript::TYPE_FLOAT:
											{
												float floatValue;
												if (arrayAccessStatementAsScriptVariable.getFloatValue(floatValue) == true) {
													generatedDefinitions+= lamdaIndent + "// Miniscript transpilation for a " + (condition == true?"condition":"statement") + " array access statement, statement index " + to_string(statement.statementIdx) + ", argument indices " + MiniScript::getArgumentIndicesAsString(nextArgumentIndices, ", ") + ", array access statement index " + to_string(arrayAccessStatementIdx) + "\n";
													generatedDefinitions+= lamdaIndent + "auto array_access_statement_" + to_string(statement.statementIdx) + "_" + MiniScript::getArgumentIndicesAsString(nextArgumentIndices, "_") + "_" + to_string(arrayAccessStatementIdx) + " = [&](const Statement& statement) -> Variable {" + "\n";
													generatedDefinitions+= lamdaIndent + "	return Variable(static_cast<int64_t>(" + to_string(static_cast<int64_t>(floatValue)) + "ll));" + "\n";
													generatedDefinitions+= lamdaIndent + "};" + "\n";
												}
												// literals
												arrayAccessStatementIdx++;
												//
												continue;
											}
										default:
											break;
									}
									// variable?
									if (StringTools::startsWith(arrayAccessStatementString, "$") == true) arrayAccessStatementString = "getVariable(\"" + arrayAccessStatementString + "\")";
									// parse array access statment at current index
									string_view arrayAccessMethodName;
									vector<string_view> arrayAccessArguments;
									string accessObjectMemberStatement;
									// create a pseudo statement (information)
									MiniScript::Statement arrayAccessStatement(
										statement.line,
										statement.statementIdx,
										arrayAccessStatementString,
										arrayAccessStatementString,
										MiniScript::STATEMENTIDX_NONE
									);
									// parse script statement
									if (miniScript->parseStatement(string_view(arrayAccessStatementString), arrayAccessMethodName, arrayAccessArguments, arrayAccessStatement, accessObjectMemberStatement) == false) {
										break;
									}
									// create syntax tree for this array access
									MiniScript::SyntaxTreeNode arrayAccessSyntaxTree;
									if (miniScript->createStatementSyntaxTree(scriptIdx, arrayAccessMethodName, arrayAccessArguments, arrayAccessStatement, arrayAccessSyntaxTree) == false) {
										break;
									}

									//
									string transpiledCode;
									auto statementIdx = MiniScript::STATEMENTIDX_FIRST;
									auto scriptStateChanged = false;
									auto scriptStopped = false;
									vector<string >enabledNamedConditions;
									transpileStatement(
										miniScript,
										transpiledCode,
										arrayAccessSyntaxTree,
										arrayAccessStatement,
										scriptConditionIdx,
										scriptIdx,
										statementIdx,
										methodCodeMap,
										allMethods,
										scriptStateChanged,
										scriptStopped,
										enabledNamedConditions,
										0,
										{},
										"Variable()",
										"return returnValue;",
										1
									);
									generatedDefinitions+= lamdaIndent + "// Miniscript transpilation for array access statement, statement index " + to_string(statement.statementIdx) + ", argument indices " + MiniScript::getArgumentIndicesAsString(nextArgumentIndices, ", ") + ", array access statement index " + to_string(arrayAccessStatementIdx) + "\n";
									generatedDefinitions+= lamdaIndent + "auto array_access_statement_" + to_string(statement.statementIdx) + "_" + MiniScript::getArgumentIndicesAsString(nextArgumentIndices, "_") + "_" + to_string(arrayAccessStatementIdx) + " = [&](const Statement& statement) -> Variable {" + "\n";
									generatedDefinitions+= lamdaIndent + "\t" + "// MiniScript setup" + "\n";
									generatedDefinitions+= lamdaIndent + "\t" + "auto miniScript = this;" + "\n";
									generatedDefinitions+= transpiledCode;
									generatedDefinitions+= lamdaIndent + "};" + "\n";
									//
									arrayAccessStatementIdx++;
								}
							}
						}
					}
				}
				//
				auto argumentIdx = 0;
				for (const auto& argument: syntaxTree.arguments) {
					//
					auto nextArgumentIndices = argumentIndices;
					nextArgumentIndices.push_back(argumentIdx);
					//
					generateArrayAccessMethods(
						miniScript,
						generatedDefinitions,
						miniScriptClassName,
						scriptConditionIdx,
						scriptIdx,
						methodName,
						argument,
						statement,
						methodCodeMap,
						allMethods,
						condition,
						nextArgumentIndices,
						depth
					);
					//
					argumentIdx++;
				}
			}
			break;
		case MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_EXECUTE_FUNCTION:
			{
				auto argumentIdx = 1;
				for (const auto& argument: syntaxTree.arguments) {
					//
					auto nextArgumentIndices = argumentIndices;
					nextArgumentIndices.push_back(argumentIdx);
					//
					generateArrayAccessMethods(
						miniScript,
						generatedDefinitions,
						miniScriptClassName,
						scriptConditionIdx,
						scriptIdx,
						methodName,
						argument,
						statement,
						methodCodeMap,
						allMethods,
						condition,
						nextArgumentIndices,
						depth
					);
					//
					argumentIdx++;
				}
				//
				break;
			}
		default:
			break;
	}
}

void Transpiler::generateEvaluateMemberAccessArrays(
	MiniScript* miniScript,
	vector<string>& generatedDeclarations,
	vector<string>& generatedDefinitions
) {
	//
	auto scriptMethods = miniScript->getMethods();
	auto allMethods = getAllClassesMethodNamesSorted(miniScript);
	auto methodsByClasses = getClassesMethodNames(miniScript);
	generatedDeclarations.push_back("// evaluate member access constants");
	generatedDeclarations.push_back("static constexpr int EVALUATEMEMBERACCESSARRAYIDX_NONE { -1 };");
	auto methodIdx = 0;
	for (const auto& method: allMethods) {
		generatedDeclarations.push_back("static constexpr int EVALUATEMEMBERACCESSARRAYIDX_" + StringTools::toUpperCase(method) + " { " + to_string(methodIdx) + " };");
		methodIdx++;
	}
	generatedDeclarations.push_back("");
	generatedDeclarations.push_back("// evaluate member access arrays");
	generatedDeclarations.push_back(
		"array<array<Method*, " +
		to_string(methodIdx) +
		">, " +
		to_string((static_cast<int>(MiniScript::TYPE_PSEUDO_DATATYPES + MiniScript::getDataTypes().size()) - static_cast<int>(MiniScript::TYPE_STRING))) +
		"> evaluateMemberAccessArrays {};"
	);
	generatedDefinitions.push_back("evaluateMemberAccessArrays = {};");
	for (auto typeIdx = static_cast<int>(MiniScript::TYPE_STRING); typeIdx < static_cast<int>(MiniScript::TYPE_PSEUDO_DATATYPES + MiniScript::getDataTypes().size()); typeIdx++) {
		const auto& className = MiniScript::Variable::getTypeAsString(static_cast<MiniScript::VariableType>(typeIdx));
		const auto& methods = methodsByClasses[className];
		auto methodIdx = 0;
		for (const auto& method: allMethods) {
			//
			if (std::find(methods.begin(), methods.end(), method) == methods.end()) {
				methodIdx++;
				continue;
			}
			//
			generatedDefinitions.push_back("evaluateMemberAccessArrays[" + to_string(typeIdx - static_cast<int>(MiniScript::TYPE_STRING)) + "][" + "EVALUATEMEMBERACCESSARRAYIDX_" + StringTools::toUpperCase(method) + "] = getMethod(\"" + className + "::" + method + "\");");
			methodIdx++;
		}
	}
}

void Transpiler::generateArrayMapSetVariable(
	MiniScript* miniScript,
	int scriptConditionIdx,
	int scriptIdx,
	const MiniScript::Variable& variable,
	const unordered_map<string, vector<string>>& methodCodeMap,
	const unordered_set<string>& allMethods,
	const string& methodName,
	bool condition,
	const string& miniScriptClassName,
	string& generatedDefinitions,
	int depth,
	int initializerDepth,
	const string& postStatement
) {
	//
	string headerIndent = "\t";
	auto indent = StringTools::indent(string(), "\t", initializerDepth + depth);
	switch (variable.getType()) {
		case MiniScript::TYPE_NULL:
			{
				generatedDefinitions+= indent + "{" + "\n";
				generatedDefinitions+= indent + "\t" + "Variable variableD" + to_string(initializerDepth) + ";" + "\n";
				generatedDefinitions+= indent + "\t" + postStatement + "\n";
				generatedDefinitions+= indent + "}" + "\n";
			}
			break;
		case MiniScript::TYPE_BOOLEAN:
			{
				bool value;
				variable.getBooleanValue(value);
				generatedDefinitions+= indent + "{" + "\n";
				generatedDefinitions+= indent + "\t" + "Variable variableD" + to_string(initializerDepth) + "(" + (value == true?"true":"false") + ");" + "\n";
				generatedDefinitions+= indent + "\t" + postStatement + "\n";
				generatedDefinitions+= indent + "}" + "\n";
			}
			break;
		case MiniScript::TYPE_INTEGER:
			{
				int64_t value;
				variable.getIntegerValue(value);
				generatedDefinitions+= indent + "{" + "\n";
				generatedDefinitions+= indent + "\t" + "Variable variableD" + to_string(initializerDepth) + "(static_cast<int64_t>(" + to_string(value) + "ll));" + "\n";
				generatedDefinitions+= indent + "\t" + postStatement + "\n";
				generatedDefinitions+= indent + "}" + "\n";
			}
			break;
		case MiniScript::TYPE_FLOAT:
			{
				float value;
				variable.getFloatValue(value);
				generatedDefinitions+= indent + "{" + "\n";
				generatedDefinitions+= indent + "\t" + "Variable variableD" + to_string(initializerDepth) + "(" + to_string(value) + "f);" + "\n";
				generatedDefinitions+= indent + "\t" + postStatement + "\n";
				generatedDefinitions+= indent + "}" + "\n";
			}
			break;
		case MiniScript::TYPE_FUNCTION_CALL:
			{
				//
				const auto& statement = variable.getInitializer()->getStatement();
				string transpiledCode;
				auto statementIdx = MiniScript::STATEMENTIDX_FIRST;
				auto scriptStateChanged = false;
				auto scriptStopped = false;
				vector<string>enabledNamedConditions;
				transpileStatement(
					miniScript,
					transpiledCode,
					*variable.getInitializer()->getSyntaxTree(),
					statement,
					scriptConditionIdx,
					scriptIdx,
					statementIdx,
					methodCodeMap,
					allMethods,
					scriptStateChanged,
					scriptStopped,
					enabledNamedConditions,
					0,
					{},
					"Variable()",
					"const auto& variableD" + to_string(initializerDepth) + " = returnValue; " + postStatement + "\n", 1
				);
				generatedDefinitions+= transpiledCode;
			}
			break;
		case MiniScript::TYPE_FUNCTION_ASSIGNMENT:
			{
				string function;
				auto functionScriptIdx = MiniScript::SCRIPTIDX_NONE;
				variable.getFunctionValue(function, functionScriptIdx);
				function = StringTools::replace(StringTools::replace(function, "\\", "\\\\"), "\"", "\\\"");
				//
				generatedDefinitions+= indent + "{" + "\n";
				generatedDefinitions+= indent + "\t" + "Variable variableD" + to_string(initializerDepth) + ";" + "\n";
				generatedDefinitions+= indent + "\t" + "variableD" + to_string(initializerDepth) + ".setFunctionAssignment(\"" + function + "\", " + to_string(functionScriptIdx) + ");" + "\n";
				generatedDefinitions+= indent + "\t" + postStatement + "\n";
				generatedDefinitions+= indent + "}" + "\n";
			}
			break;
		case MiniScript::TYPE_STACKLET_ASSIGNMENT:
			{
				string stacket;
				auto stackletScriptIdx = MiniScript::SCRIPTIDX_NONE;
				variable.getStackletValue(stacket, stackletScriptIdx);
				stacket = StringTools::replace(StringTools::replace(stacket, "\\", "\\\\"), "\"", "\\\"");
				//
				generatedDefinitions+= indent + "{" + "\n";
				generatedDefinitions+= indent + "\t" + "Variable variableD" + to_string(initializerDepth) + ";" + "\n";
				generatedDefinitions+= indent + "\t" + "variableD" + to_string(initializerDepth) + ".setStackletAssignment(\"" + stacket + "\", " + to_string(stackletScriptIdx) + ");" + "\n";
				generatedDefinitions+= indent + "\t" + postStatement + "\n";
				generatedDefinitions+= indent + "}" + "\n";
			}
			break;
		case MiniScript::TYPE_STRING:
			{
				string value;
				variable.getStringValue(value);
				value = StringTools::replace(StringTools::replace(value, "\\", "\\\\"), "\"", "\\\"");
				//
				generatedDefinitions+= indent + "{" + "\n";
				generatedDefinitions+= indent + "\t" + "Variable variableD" + to_string(initializerDepth) + "(string(\"" + value + "\"));" + "\n";
				generatedDefinitions+= indent + "\t" + postStatement + "\n";
				generatedDefinitions+= indent + "}" + "\n";
			}
			break;
		case MiniScript::TYPE_ARRAY:
			{
				if (initializerDepth == 0) {
					generatedDefinitions+= string() + "{" + "\n";
					generatedDefinitions+= indent + "\t" + "// MiniScript setup" + "\n";
					generatedDefinitions+= indent + "\t" + "auto miniScript = this;" + "\n";
					generatedDefinitions+= indent + "\t" + "//" + "\n";
				} else {
					generatedDefinitions+= indent + "{" + "\n";
				}
				generatedDefinitions+= indent + "\t" + "Variable variableD" + to_string(initializerDepth) + ";" + "\n";
				generatedDefinitions+= indent + "\t" + "variableD" + to_string(initializerDepth) + ".setType(TYPE_ARRAY);" + "\n";
				const auto arrayValue = variable.getArrayPointer();
				for (const auto arrayEntry: *arrayValue) {
					generateArrayMapSetVariable(
						miniScript,
						scriptConditionIdx,
						scriptIdx,
						*arrayEntry,
						methodCodeMap,
						allMethods,
						methodName,
						condition,
						miniScriptClassName,
						generatedDefinitions,
						depth,
						initializerDepth + 1,
						"variableD" + to_string(initializerDepth) + ".pushArrayEntry(variableD" + to_string(initializerDepth + 1) + ");"
					);
				}
				generatedDefinitions+= indent + "\t" + postStatement + "\n";
				generatedDefinitions+= indent + "}";
				generatedDefinitions+= initializerDepth == 0?";":"\n";
			}
			break;
		case MiniScript::TYPE_MAP:
			{
				if (initializerDepth == 0) {
					generatedDefinitions+= string() + "{" + "\n";
					generatedDefinitions+= indent + "\t" + "// MiniScript setup" + "\n";
					generatedDefinitions+= indent + "\t" + "auto miniScript = this;" + "\n";
					generatedDefinitions+= indent + "\t" + "//" + "\n";
				} else {
					generatedDefinitions+= indent + "{" + "\n";
				}
				generatedDefinitions+= indent + "\t" + "Variable variableD" + to_string(initializerDepth) + ";" + "\n";
				generatedDefinitions+= indent + "\t" + "variableD" + to_string(initializerDepth) + ".setType(TYPE_MAP);" + "\n";
				const auto mapValue = variable.getMapPointer();
				for (const auto& [mapEntryName, mapEntryValue]: *mapValue) {
					auto mapEntryNameEscaped = StringTools::replace(StringTools::replace(mapEntryName, "\\", "\\\\"), "\"", "\\\"");
					generateArrayMapSetVariable(
						miniScript,
						scriptConditionIdx,
						scriptIdx,
						*mapEntryValue,
						methodCodeMap,
						allMethods,
						methodName,
						condition,
						miniScriptClassName,
						generatedDefinitions,
						depth,
						initializerDepth + 1,
						"variableD" + to_string(initializerDepth) + ".setMapEntry(\"" + mapEntryNameEscaped + "\", variableD" + to_string(initializerDepth + 1) + ");"
					);
				}
				generatedDefinitions+= indent + "\t" + postStatement + "\n";
				generatedDefinitions+= indent + "}";
				generatedDefinitions+= initializerDepth == 0?";":"\n";
			}
			break;
		case MiniScript::TYPE_SET:
			{
				if (initializerDepth == 0) {
					generatedDefinitions+= string() + "{" + "\n";
					generatedDefinitions+= indent + "\t" + "// MiniScript setup" + "\n";
					generatedDefinitions+= indent + "\t" + "auto miniScript = this;" + "\n";
					generatedDefinitions+= indent + "\t" + "//" + "\n";
				} else {
					generatedDefinitions+= indent + "{" + "\n";
				}
				generatedDefinitions+= indent + "\t" + "Variable variableD" + to_string(initializerDepth) + ";" + "\n";
				generatedDefinitions+= indent + "\t" + "variableD" + to_string(initializerDepth) + ".setType(TYPE_SET);" + "\n";
				const auto setValue = variable.getSetPointer();
				for (const auto& key: *setValue) {
					generatedDefinitions+= indent + "\t" + "variableD" + to_string(initializerDepth) + ".insertSetKey(\"" + key + "\");" + "\n";
				}
				generatedDefinitions+= indent + "\t" + postStatement + "\n";
				generatedDefinitions+= indent + "}";
				generatedDefinitions+= initializerDepth == 0?";":"\n";
			}
			break;
		default: break;
	}
}

void Transpiler::generateArrayMapSetInitializer(
	MiniScript* miniScript,
	string& generatedDefinitions,
	int scriptConditionIdx,
	int scriptIdx,
	const string& miniScriptClassName,
	const string& methodName,
	const MiniScript::SyntaxTreeNode& syntaxTree,
	const MiniScript::Statement& statement,
	const unordered_map<string, vector<string>>& methodCodeMap,
	const unordered_set<string>& allMethods,
	bool condition,
	const vector<int>& argumentIndices,
	int depth
	) {
	//
	auto indent = StringTools::indent(string(), "\t", depth);
	//
	switch (syntaxTree.type) {
		case MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_LITERAL:
			{
				switch(syntaxTree.value.getType()) {
					case MiniScript::TYPE_ARRAY:
					case MiniScript::TYPE_MAP:
					case MiniScript::TYPE_SET:
						{
							//
							string generatedInitializerDefinitions;
							//
							generateArrayMapSetVariable(
								miniScript,
								scriptConditionIdx,
								scriptIdx,
								syntaxTree.value,
								methodCodeMap,
								allMethods,
								methodName,
								condition,
								miniScriptClassName,
								generatedInitializerDefinitions,
								depth,
								0,
								"return variableD0;"
							);
							//
							generatedDefinitions+= indent + "// Miniscript transpilation for array/map/set initializer, statement index " + to_string(statement.statementIdx) + ", argument indices " + MiniScript::getArgumentIndicesAsString(argumentIndices, ", ") + "\n";
							generatedDefinitions+= indent + "auto initializer_" + to_string(statement.statementIdx) + "_" + MiniScript::getArgumentIndicesAsString(argumentIndices, "_") + " = [&](const Statement& statement) -> Variable ";
							generatedDefinitions+= generatedInitializerDefinitions;
							//
							break;
						}
					default: break;
				}
				//
				break;
			}
		case MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_EXECUTE_METHOD:
			{
				auto argumentIdx = 0;
				for (const auto& argument: syntaxTree.arguments) {
					//
					auto nextArgumentIndices = argumentIndices;
					nextArgumentIndices.push_back(argumentIdx);
					//
					generateArrayMapSetInitializer(
						miniScript,
						generatedDefinitions,
						scriptConditionIdx,
						scriptIdx,
						miniScriptClassName,
						methodName,
						argument,
						statement,
						methodCodeMap,
						allMethods,
						condition,
						nextArgumentIndices,
						depth
					);
					//
					argumentIdx++;
				}
				break;
			}
		case MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_EXECUTE_FUNCTION:
			{
				auto argumentIdx = 1; // TODO: check me!
				for (const auto& argument: syntaxTree.arguments) {
					//
					auto nextArgumentIndices = argumentIndices;
					nextArgumentIndices.push_back(argumentIdx);
					//
					generateArrayMapSetInitializer(
						miniScript,
						generatedDefinitions,
						scriptConditionIdx,
						scriptIdx,
						miniScriptClassName,
						methodName,
						argument,
						statement,
						methodCodeMap,
						allMethods,
						condition,
						nextArgumentIndices,
						depth
					);
					//
					argumentIdx++;
				}
				//
				break;
			}
		default:
			break;
	}
}

bool Transpiler::transpileStatement(
	MiniScript* miniScript,
	string& generatedCode,
	const MiniScript::SyntaxTreeNode& syntaxTree,
	const MiniScript::Statement& statement,
	int scriptConditionIdx,
	int scriptIdx,
	int& statementIdx,
	const unordered_map<string, vector<string>>& methodCodeMap,
	const unordered_set<string>& allMethods,
	bool& scriptStateChanged,
	bool& scriptStopped,
	vector<string>& enabledNamedConditions,
	int depth,
	const vector<int>& argumentIndices,
	const string& returnValue,
	const string& injectCode,
	int additionalIndent) {
	//
	statementIdx++;
	auto currentStatementIdx = statementIdx;

	// indenting
	string minIndentString = "\t";
	string depthIndentString;
	for (auto i = 0; i < depth + additionalIndent; i++) depthIndentString+= "\t";

	//
	struct ArrayAccessStatement {
		ArrayAccessStatement(
			int argumentIdx,
			int statementIdx,
			int leftIdx,
			int rightIdx,
			string statementMethod
		):
			argumentIdx(argumentIdx),
			statementIdx(statementIdx),
			leftIdx(leftIdx),
			rightIdx(rightIdx),
			statementMethod(statementMethod)
		{}
		int argumentIdx;
		int statementIdx;
		int leftIdx;
		int rightIdx;
		string statementMethod;
	};

	//
	vector<ArrayAccessStatement> arrayAccessStatements;

	//
	switch (syntaxTree.type) {
		case MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_EXECUTE_FUNCTION:
		case MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_EXECUTE_STACKLET:
			{
				// check script user functions
				auto functionStackletScriptIdx = syntaxTree.getScriptIdx();
				if (functionStackletScriptIdx != MiniScript::SCRIPTIDX_NONE) {
					// have a wrapping script.call/script.callStacklet call
					MiniScript::SyntaxTreeNode callSyntaxTreeNode;
					callSyntaxTreeNode.type = MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_EXECUTE_METHOD;
					callSyntaxTreeNode.value =
						syntaxTree.type == MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_EXECUTE_FUNCTION?
							MiniScript::METHOD_SCRIPTCALLBYINDEX:
							MiniScript::METHOD_SCRIPTCALLSTACKLETBYINDEX;
					// construct argument for name of function/stacklet
					MiniScript::SyntaxTreeNode callArgumentSyntaxTreeNode;
					callArgumentSyntaxTreeNode.type = MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_LITERAL;
					callArgumentSyntaxTreeNode.value = MiniScript::Variable(functionStackletScriptIdx);
					// add argumnet for name of function/stacklet
					callSyntaxTreeNode.arguments.push_back(callArgumentSyntaxTreeNode);
					// stacklets have no arguments when calling them, functions do have them
					if (syntaxTree.type == MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_EXECUTE_FUNCTION) {
						// add original parameter to call syntaxTree
						for (const auto& argument: syntaxTree.arguments) {
							callSyntaxTreeNode.arguments.push_back(argument);
						}
					}
					// assign script.call/script.callStacklet method
					auto method = miniScript->getMethod(callSyntaxTreeNode.value.getValueAsString());
					if (method == nullptr) {
						Console::printLine("Transpiler::transpileStatement(): method code not found: '" + callSyntaxTreeNode.value.getValueAsString() + "'");
						return false;
					}
					callSyntaxTreeNode.setMethod(method);
					return transpileStatement(
						miniScript,
						generatedCode,
						callSyntaxTreeNode,
						statement,
						scriptConditionIdx,
						scriptIdx,
						statementIdx,
						methodCodeMap,
						allMethods,
						scriptStateChanged,
						scriptStopped,
						enabledNamedConditions,
						depth,
						argumentIndices,
						returnValue,
						injectCode,
						additionalIndent
					);
				} else {
					Console::printLine("Transpiler::transpileStatement(): Function/stacklet not found: '" + syntaxTree.value.getValueAsString() + "'");
					return false;
				}
				//
				break;
			}
		case MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_EXECUTE_METHOD:
			//
			if ((scriptConditionIdx != MiniScript::SCRIPTIDX_NONE ||
				scriptIdx != MiniScript::SCRIPTIDX_NONE) &&
				(syntaxTree.value.getValueAsString() == "getMethodArgumentVariable" ||
				syntaxTree.value.getValueAsString() == "getVariable" ||
				syntaxTree.value.getValueAsString() == "getVariableReference" ||
				syntaxTree.value.getValueAsString() == "setVariable" ||
				syntaxTree.value.getValueAsString() == "setVariableReference" ||
				syntaxTree.value.getValueAsString() == "setConstant")) {
				//
				for (auto argumentIdx = 0; argumentIdx < syntaxTree.arguments.size(); argumentIdx++) {
					//
					auto nextArgumentIndices = argumentIndices;
					nextArgumentIndices.push_back(argumentIdx);
					//
					auto argumentString = StringTools::replace(StringTools::replace(syntaxTree.arguments[argumentIdx].value.getValueAsString(), "\\", "\\\\"), "\"", "\\\"");
					auto arrayAccessStatementIdx = 0;
					auto arrayAccessStatementLeftIdx = -1;
					auto arrayAccessStatementRightIdx = -1;
					auto quote = '\0';
					auto bracketCount = 0;
					auto lc = '\0';
					for (auto i = 0; i < argumentString.size(); i++) {
						auto c = argumentString[i];
						// handle quotes
						if ((c == '"' || c == '\'') && lc != '\\') {
							if (quote == '\0') {
								quote = c;
							} else
							if (quote == c) {
								quote = '\0';
							}
						} else
						if (c == '[') {
							if (bracketCount == 0) arrayAccessStatementLeftIdx = i;
							bracketCount++;
						} else
						if (c == ']') {
							bracketCount--;
							if (bracketCount == 0) {
								arrayAccessStatementRightIdx = i;
								//
								auto arrayAccessStatementString = StringTools::substring(argumentString, arrayAccessStatementLeftIdx + 1, arrayAccessStatementRightIdx);
								// array append operator []
								if (arrayAccessStatementString.empty() == true) {
									//
									arrayAccessStatementIdx++;
									//
									continue;
								}
								//
								const auto& script = miniScript->getScripts()[scriptConditionIdx != MiniScript::SCRIPTIDX_NONE?scriptConditionIdx:scriptIdx];
								//
								auto arrayAccessStatementMethod = string() + "array_access_statement_" + to_string(statement.statementIdx) + "_" + MiniScript::getArgumentIndicesAsString(nextArgumentIndices, "_") + "_" + to_string(arrayAccessStatementIdx);
								//
								generatedCode+= minIndentString + depthIndentString + "// we will use " + arrayAccessStatementMethod + "() to determine array access index"+ "\n";
								//
								arrayAccessStatements.emplace_back(
									argumentIdx,
									arrayAccessStatementIdx,
									arrayAccessStatementLeftIdx,
									arrayAccessStatementRightIdx,
									arrayAccessStatementMethod
								);
								//
								arrayAccessStatementIdx++;
							}
						}
						//
						lc = c;
					}
				}
			}
			//
			break;
		default:
			Console::printLine("Transpiler::transpileStatement(): " + miniScript->getStatementInformation(statement) + ": function or method call expected, but got literal or 'none' syntaxTree");
			return false;

	}

	//
	auto method = syntaxTree.value.getValueAsString();

	// find method code in method code map
	auto methodCodeMapIt = methodCodeMap.find(method);
	if (methodCodeMapIt == methodCodeMap.end()) {
		Console::printLine("Transpiler::transpileStatement(): method code not found: '" + method + "'");
		return false;
	}
	const auto& methodCode = methodCodeMapIt->second;

	// script method
	auto scriptMethod = miniScript->getMethod(string(method));
	if (scriptMethod == nullptr) {
		Console::printLine("Transpiler::transpileStatement(): method not found: '" + method + "'");
		return false;
	}

	// comment about current statement
	generatedCode+= minIndentString + depthIndentString;
	generatedCode+= "// " + (depth > 0?"depth = " + to_string(depth):"") + (argumentIndices.empty() == false?" / argument index = " + to_string(argumentIndices.back()):"");
	generatedCode+= depth > 0 || argumentIndices.empty() == false?": ":"";
	generatedCode+= syntaxTree.value.getValueAsString() + "(" + miniScript->getArgumentsAsString(syntaxTree.arguments) + ")";
	generatedCode+= "\n";

	// argument values header
	generatedCode+= minIndentString + depthIndentString + "{" + "\n";

	// argument indices
	auto parentArgumentIdx = argumentIndices.size() >= 2?argumentIndices[argumentIndices.size() - 2]:MiniScript::ARGUMENTIDX_NONE;
	auto argumentIdx = argumentIndices.empty() == false?argumentIndices.back():MiniScript::ARGUMENTIDX_NONE;

	// statement
	if (depth == 0) {
		generatedCode+= minIndentString + depthIndentString + "\t" + "// statement setup" + "\n";
		if (scriptConditionIdx != MiniScript::SCRIPTIDX_NONE) {
			generatedCode+= minIndentString + depthIndentString + "\t" + "const auto& statement = scripts[" + to_string(scriptConditionIdx) + "].conditionStatement;" + "\n";
		} else
		if (scriptIdx != MiniScript::SCRIPTIDX_NONE) {
			generatedCode+= minIndentString + depthIndentString + "\t" + "const auto& statement = scripts[" + to_string(scriptIdx) + "].statements[" + to_string(statement.statementIdx) + "];" + "\n";
		}
		generatedCode+= minIndentString + depthIndentString + "\t" + "getScriptState().statementIdx = statement.statementIdx;" + "\n";
	}

	// construct argument values
	string indent = "\t";
	{
		vector<string> argumentsCode;
		if (depth > 0) {
			argumentsCode.push_back("auto& returnValue = argumentsD" + to_string(depth - 1) + (parentArgumentIdx != MiniScript::ARGUMENTIDX_NONE?"AIDX" + to_string(parentArgumentIdx):"") + "[" + to_string(argumentIdx) + "];");
		} else {
			argumentsCode.push_back("Variable returnValue;");
		}
		argumentsCode.push_back("array<Variable, " + to_string(syntaxTree.arguments.size()) + "> arguments {");

		// construct argument values
		if (syntaxTree.arguments.empty() == false) {
			generatedCode+= minIndentString + depthIndentString + "\t" + "// required method code arguments" + "\n";
			auto argumentIdx = 0;
			for (const auto& argument: syntaxTree.arguments) {
				//
				auto nextArgumentIndices = argumentIndices;
				nextArgumentIndices.push_back(argumentIdx);
				//
				auto lastArgument = argumentIdx == syntaxTree.arguments.size() - 1;
				switch (argument.type) {
					case MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_LITERAL:
						{
							switch (argument.value.getType())  {
								case MiniScript::TYPE_NULL:
									argumentsCode.push_back(indent + "Variable()" + (lastArgument == false?",":""));
									break;
								case MiniScript::TYPE_BOOLEAN:
									{
										bool value;
										argument.value.getBooleanValue(value);
										argumentsCode.push_back(indent + "Variable(" + (value == true?"true":"false") + ")" + (lastArgument == false?",":""));
									}
									break;
								case MiniScript::TYPE_INTEGER:
									{
										int64_t value;
										argument.value.getIntegerValue(value);
										argumentsCode.push_back(indent +  + "Variable(static_cast<int64_t>(" + to_string(value) + "ll))" + (lastArgument == false?",":""));
									}
									break;
								case MiniScript::TYPE_FLOAT:
									{
										float value;
										argument.value.getFloatValue(value);
										argumentsCode.push_back(indent +  + "Variable(" + to_string(value) + "f)" + (lastArgument == false?",":""));
									}
									break;
								case MiniScript::TYPE_STRING:
								case MiniScript::TYPE_FUNCTION_ASSIGNMENT:
								case MiniScript::TYPE_STACKLET_ASSIGNMENT:
									{
										string value;
										auto scriptIdx = MiniScript::SCRIPTIDX_NONE;
										// get it, while its hot
										if (argument.value.getType() == MiniScript::TYPE_FUNCTION_ASSIGNMENT) {
											argument.value.getFunctionValue(value, scriptIdx);
										} else
										if (argument.value.getType() == MiniScript::TYPE_STACKLET_ASSIGNMENT) {
											argument.value.getStackletValue(value, scriptIdx);
										} else {
											argument.value.getStringValue(value);
										}
										value = StringTools::replace(StringTools::replace(value, "\\", "\\\\"), "\"", "\\\"");
										// take array access statements into account
										auto arrayAccessStatementOffset = 0;
										for (auto& arrayAccessStatement: arrayAccessStatements) {
											if (arrayAccessStatement.argumentIdx != argumentIdx) continue;
											string arrayAccessStatementMethodCall = "\" + " + arrayAccessStatement.statementMethod + "(statement).getValueAsString() + \"";
											value =
												StringTools::substring(value, 0, arrayAccessStatement.leftIdx + 1 + arrayAccessStatementOffset) +
												arrayAccessStatementMethodCall +
												StringTools::substring(value, arrayAccessStatement.rightIdx + arrayAccessStatementOffset, value.size());
											arrayAccessStatementOffset-= (arrayAccessStatement.rightIdx - (arrayAccessStatement.leftIdx + 1)) - arrayAccessStatementMethodCall.size();
										}
										//
										argumentsCode.push_back(indent + "Variable(static_cast<VariableType>(" + to_string(argument.value.getType()) + "), string(\"" + value + "\"), " + to_string(scriptIdx) + ")" + (lastArgument == false?",":""));
									}
									//
									break;
								case MiniScript::TYPE_ARRAY:
								case MiniScript::TYPE_MAP:
								case MiniScript::TYPE_SET:
									{
										const auto& script = miniScript->getScripts()[scriptConditionIdx != MiniScript::SCRIPTIDX_NONE?scriptConditionIdx:scriptIdx];
										auto methodName = createMethodName(miniScript, scriptIdx);										//
										auto initializerMethod = string() + "initializer_" + to_string(statement.statementIdx) + "_" + miniScript->getArgumentIndicesAsString(nextArgumentIndices, "_");
										argumentsCode.push_back(indent + initializerMethod + "(statement)" + (lastArgument == false?",":""));
									}
									break;
								default:
									{
										Console::printLine("Transpiler::transpileStatement(): " + miniScript->getStatementInformation(statement) + ": '" + argument.value.getAsString() + "': unknown argument type: " + argument.value.getTypeAsString());
										break;
									}
							}
							break;
						}
					case MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_EXECUTE_FUNCTION:
					case MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_EXECUTE_STACKLET:
						{
							argumentsCode.push_back(indent + "Variable()" + (lastArgument == false?",":"") + " // arguments[" + to_string(argumentIdx) + "] --> returnValue of " + argument.value.getValueAsString() + "(" + miniScript->getArgumentsAsString(argument.arguments) + ")");
							break;
						}
					case MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_EXECUTE_METHOD:
						{
							argumentsCode.push_back(indent + "Variable()" + (lastArgument == false?",":"") + " // arguments[" + to_string(argumentIdx) + "] --> returnValue of " + argument.value.getValueAsString() + "(" + miniScript->getArgumentsAsString(argument.arguments) + ")");
							break;
						}
					default:
						break;
				}
				//
				argumentIdx++;
			}
		}
		// end of arguments initialization
		argumentsCode.push_back("};");

		//
		argumentsCode.push_back("array<Variable, " + to_string(syntaxTree.arguments.size()) + ">& argumentsD" + to_string(depth) + (argumentIdx != MiniScript::ARGUMENTIDX_NONE?"AIDX" + to_string(argumentIdx):"") + " = arguments;");

		// argument values header
		for (const auto& codeLine: argumentsCode) {
			generatedCode+= minIndentString + depthIndentString + "\t" + codeLine + "\n";
		}
	}

	// enabled named conditions
	if (method == MiniScript::METHOD_ENABLENAMEDCONDITION && syntaxTree.arguments.empty() == false) {
		if (syntaxTree.arguments.size() != 1) {
			Console::printLine("Transpiler::transpileStatement(): " + miniScript->getStatementInformation(statement) + ": " + MiniScript::METHOD_ENABLENAMEDCONDITION + "(): expected string argument @ 0");
		} else {
			string name = syntaxTree.arguments[0].value.getValueAsString();
			enabledNamedConditions.erase(
				remove(
					enabledNamedConditions.begin(),
					enabledNamedConditions.end(),
					name
				),
				enabledNamedConditions.end()
			);
			enabledNamedConditions.push_back(name);
		}
	} else
	if (method == MiniScript::METHOD_DISABLENAMEDCONDITION && syntaxTree.arguments.empty() == false) {
		if (syntaxTree.arguments.size() != 1) {
			Console::printLine("Transpiler::transpileStatement(): " + miniScript->getStatementInformation(statement) + ": " + MiniScript::METHOD_DISABLENAMEDCONDITION + "(): expected string argument @ 0");
		} else {
			string name = syntaxTree.arguments[0].value.getValueAsString();
			enabledNamedConditions.erase(
				remove(
					enabledNamedConditions.begin(),
					enabledNamedConditions.end(),
					name
				),
				enabledNamedConditions.end()
			);
		}
	}

	// transpile method/function call argument
	{
		auto argumentIdx = 0;
		for (const auto& argument: syntaxTree.arguments) {
			switch (argument.type) {
				case MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_EXECUTE_FUNCTION:
				case MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_EXECUTE_STACKLET:
				case MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_EXECUTE_METHOD:
					{
						//
						auto nextArgumentIndices = argumentIndices;
						nextArgumentIndices.push_back(argumentIdx);

						//
						if (transpileStatement(
							miniScript,
							generatedCode,
							argument,
							statement,
							scriptConditionIdx,
							scriptIdx,
							statementIdx,
							methodCodeMap,
							allMethods,
							scriptStateChanged,
							scriptStopped,
							enabledNamedConditions,
							depth + 1,
							nextArgumentIndices,
							returnValue,
							string(),
							additionalIndent
						) == false) {
							Console::printLine("Transpiler::transpileStatement(): transpileStatement(): " + miniScript->getStatementInformation(statement) + ": '" + syntaxTree.value.getValueAsString() + "(" + miniScript->getArgumentsAsString(syntaxTree.arguments) + ")" + "': transpile error");
						}
					}
					//
					break;
				default:
					//
					break;
			}
			argumentIdx++;
		}
	}

	// special case: inject EVALUATEMEMBERACCESS_MEMBER for "internal.script.evaluateMemberAccess"
	if (scriptMethod != nullptr && scriptMethod->getMethodName() == "internal.script.evaluateMemberAccess") {
		//
		auto callArgumentIdx = 0;
		//
		if (syntaxTree.arguments[0].value.getType() != MiniScript::TYPE_NULL) {
			generateVariableAccess(
				miniScript,
				generatedCode,
				scriptConditionIdx,
				scriptIdx,
				syntaxTree.arguments[0].value.getValueAsString(),
				minIndentString + depthIndentString + "\t",
				false,
				false,
				true,
				false,
				false,
				false,
				"auto EVALUATEMEMBERACCESS_ARGUMENT" + to_string(callArgumentIdx) + " = "
			);
		} else {
			generatedCode+= minIndentString + depthIndentString + "\t" + "auto EVALUATEMEMBERACCESS_ARGUMENT" + to_string(callArgumentIdx) + " = Variable();" + "\n";
		}
		//
		callArgumentIdx++;
		//
		vector<string> argumentVariables;
		for (auto argumentIdx = 3; argumentIdx < syntaxTree.arguments.size(); argumentIdx+=2) {
			// do we have a this variable name?
			argumentVariables.push_back(string());
			//
			if (syntaxTree.arguments[argumentIdx].value.getType() != MiniScript::TYPE_NULL) {
				//
				generateVariableAccess(
					miniScript,
					argumentVariables[argumentVariables.size() - 1],
					scriptConditionIdx,
					scriptIdx,
					syntaxTree.arguments[argumentIdx].value.getValueAsString(),
					minIndentString + depthIndentString + "\t\t",
					false,
					false,
					true,
					false,
					false,
					false,
					string(),
					",\n",
					argumentIdx,
					-1
				);
			} else {
				//
				argumentVariables[argumentVariables.size() - 1] = minIndentString + depthIndentString + "\t\t" + "Variable()," + "\n";
			}
			//
			callArgumentIdx++;
		}
		generatedCode+= minIndentString + depthIndentString + "\t" + "array<Variable, " + to_string(argumentVariables.size()) + "> EVALUATEMEMBERACCESS_ARGUMENTS" + " {" + "\n";
		for (const auto& argumentVariable: argumentVariables) generatedCode+= argumentVariable;
		generatedCode+= minIndentString + depthIndentString + "\t" + "};" + "\n";
		//
		if (allMethods.contains(syntaxTree.arguments[2].value.getValueAsString()) == true) {

			generatedCode+= minIndentString + depthIndentString + "\t" + "const auto EVALUATEMEMBERACCESS_MEMBER = EVALUATEMEMBERACCESSARRAYIDX_" + StringTools::toUpperCase(syntaxTree.arguments[2].value.getValueAsString()) + ";\n";
		} else {
			generatedCode+= minIndentString + depthIndentString + "\t" + "const auto EVALUATEMEMBERACCESS_MEMBER = EVALUATEMEMBERACCESSARRAYIDX_NONE;" + "\n";
		}
	}

	// do we have a variable look up or setting a variable?
	// 	transfer to use local method and global class variables
	if (syntaxTree.type == MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_EXECUTE_METHOD &&

		(syntaxTree.value.getValueAsString() == "getMethodArgumentVariable" ||
		syntaxTree.value.getValueAsString() == "getVariable" ||
		syntaxTree.value.getValueAsString() == "getVariableReference" ||
		syntaxTree.value.getValueAsString() == "setVariable" ||
		syntaxTree.value.getValueAsString() == "setVariableReference" ||
		syntaxTree.value.getValueAsString() == "setConstant" ||
		syntaxTree.value.getValueAsString() == "unsetVariable") &&
		syntaxTree.arguments.empty() == false &&
		syntaxTree.arguments[0].type == MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_LITERAL) {
		// generate variable access
		generateVariableAccess(
			miniScript,
			generatedCode,
			scriptConditionIdx,
			scriptIdx,
			syntaxTree.arguments[0].value.getValueAsString(),
			minIndentString + depthIndentString + "\t",
			syntaxTree.value.getValueAsString() == "getMethodArgumentVariable",
			syntaxTree.value.getValueAsString() == "getVariable",
			syntaxTree.value.getValueAsString() == "getVariableReference",
			syntaxTree.value.getValueAsString() == "setVariable",
			syntaxTree.value.getValueAsString() == "setVariableReference",
			syntaxTree.value.getValueAsString() == "setConstant"
		);
	} else {
		const auto& script = miniScript->getScripts()[scriptConditionIdx != MiniScript::SCRIPTIDX_NONE?scriptConditionIdx:scriptIdx];
		// generate code
		generatedCode+= minIndentString + depthIndentString + "\t" + "// method code: " + string(method) + "\n";
		for (auto codeLine: methodCode) {
			codeLine = StringTools::replace(codeLine, "getMethodName()", "string(\"" + string(method) + "\")");
			// replace returns with gotos
			if (StringTools::regexMatch(codeLine, "[\\ \\t]*return[\\ \\t]*;.*") == true) {
				Console::printLine("Transpiler::transpileStatement(): method '" + string(method) + "': return statement not supported!");
				return false;
			} else
			if (StringTools::regexMatch(codeLine, "[\\ \\t]*miniScript[\\ \\t]*->gotoStatementGoto[\\ \\t]*\\([\\ \\t]*statement[\\ \\t]*\\)[\\ \\t]*;[\\ \\t]*") == true) {
				if (statement.gotoStatementIdx != MiniScript::STATEMENTIDX_NONE) {
					 // find indent
					int indent = 0;
					for (auto i = 0; i < codeLine.size(); i++) {
						auto c = codeLine[i];
						if (c == '\t') {
							indent++;
						} else {
							break;
						}
					}
					string indentString;
					for (auto i = 0; i < indent; i++) indentString+= "\t";
					//
					generatedCode+= minIndentString + indentString + depthIndentString + "\t" + "goto miniscript_statement_" + to_string(statement.gotoStatementIdx) + ";\n";
				} else {
					// for now we exit the C++ method after setting the gotoStatement and reenter the C++ method
					generatedCode+= minIndentString + depthIndentString + "\t" + codeLine + " return;" + "\n";
				}
			} else
			if (StringTools::regexMatch(codeLine, "[\\ \\t]*miniScript[\\ \\t]*->gotoStatement[\\ \\t]*\\(.*\\)[\\ \\t]*;[\\ \\t]*") == true) {
				// TODO: those are the break and continue statements, we might improve this later
				//	we support break and continue with several levels, so this is not easy to be solved by iterating the statements, lets see later
				//	for now we exit the C++ method after setting the gotoStatement and reenter the C++ method
				generatedCode+= minIndentString + depthIndentString + "\t" + codeLine + " return;" + "\n";
			} else
			if (StringTools::regexMatch(codeLine, "[\\ \\t]*MINISCRIPT_METHODUSAGE_COMPLAIN[\\ \\t]*\\([\\ \\t]*(.*)\\)[\\ \\t]*;[\\ \\t]*") == true ||
				StringTools::regexMatch(codeLine, "[\\ \\t]*MINISCRIPT_METHODUSAGE_COMPLAINM[\\ \\t]*\\([\\ \\t]*(.*)[\\ \\t]*,[\\ \\t]*(.*)[\\ \\t]*\\)[\\ \\t]*;[\\ \\t]*") == true ||
				StringTools::regexMatch(codeLine, "[\\ \\t]*miniScript[\\ \\t]*->emit[\\ \\t]*\\([\\ \\t]*[a-zA-Z0-9]*[\\ \\t]*\\)[\\ \\t]*;[\\ \\t]*") == true) {
				generatedCode+= minIndentString + depthIndentString + "\t" + codeLine + (script.type == MiniScript::Script::TYPE_ON || script.type == MiniScript::Script::TYPE_ONENABLED?" MINISCRIPT_METHOD_POPSTACK(); return" + (returnValue.empty() == false?" " + returnValue:"") + ";":"") + "\n";
			} else {
				if (StringTools::regexMatch(codeLine, ".*[\\ \\t]*miniScript[\\ \\t]*->[\\ \\t]*setScriptStateState[\\ \\t]*\\([\\ \\t]*.+[\\ \\t]*\\);.*") == true) {
					scriptStateChanged = true;
				}
				if (StringTools::regexMatch(codeLine, ".*[\\ \\t]*miniScript[\\ \\t]*->[\\ \\t]*stopScriptExecutation[\\ \\t]*\\([\\ \\t]*\\);.*") == true) {
					scriptStopped = true;
				} else
				if (StringTools::regexMatch(codeLine, ".*[\\ \\t]*miniScript[\\ \\t]*->[\\ \\t]*stopRunning[\\ \\t]*\\([\\ \\t]*\\);.*") == true) {
					scriptStopped = true;
				}
				generatedCode+= minIndentString + depthIndentString + "\t" + codeLine + "\n";
			}
		}
	}

	// inject code if we have any to inject
	if (injectCode.empty() == false) {
		generatedCode+= minIndentString + depthIndentString + "\t" + injectCode + "\n";
	}

	//
	generatedCode+= minIndentString + depthIndentString + "}" + "\n";

	//
	return true;
}

bool Transpiler::transpile(MiniScript* miniScript, string& generatedCode, int scriptIdx, const unordered_map<string, vector<string>>& methodCodeMap, const unordered_set<string>& allMethods) {
	if (scriptIdx < 0 || scriptIdx >= miniScript->getScripts().size()) {
		Console::printLine("Transpiler::transpile(): invalid script index");
		return false;
	}

	//
	const auto& script = miniScript->getScripts()[scriptIdx];

	//
	Console::printLine("Transpiler::transpile(): transpiling code for " + getScriptTypeReadableName(script.type) + " = '" + script.condition + "', with name '" + script.name + "'");

	//
	string methodIndent = "\t";
	string generatedCodeHeader;

	//
	generatedCodeHeader+= methodIndent + "// script setup" + "\n";
	generatedCodeHeader+= methodIndent + "auto miniScript = this;" + "\n";
	generatedCodeHeader+= methodIndent + "getScriptState().scriptIdx = " + to_string(scriptIdx) + ";" + "\n";

	// method name
	auto methodName = createMethodName(miniScript, scriptIdx);

	//
	unordered_set<int> gotoStatementIdxSet;
	for (const auto& statement: script.statements) {
		if (statement.gotoStatementIdx != MiniScript::STATEMENTIDX_NONE) {
			gotoStatementIdxSet.insert(statement.statementIdx);
			gotoStatementIdxSet.insert(statement.gotoStatementIdx);
		}
	}

	//
	auto statementIdx = MiniScript::STATEMENTIDX_FIRST;
	vector<string> enabledNamedConditions;
	auto scriptStateChanged = false;
	for (auto scriptStatementIdx = MiniScript::STATEMENTIDX_FIRST; scriptStatementIdx < script.statements.size(); scriptStatementIdx++) {
		const auto& statement = script.statements[scriptStatementIdx];
		const auto& syntaxTree = script.syntaxTree[scriptStatementIdx];
		//
		if (scriptStateChanged == true || gotoStatementIdxSet.find(statement.statementIdx) != gotoStatementIdxSet.end()) {
			generatedCodeHeader+= methodIndent + "if (miniScriptGotoStatementIdx == " + to_string(statement.statementIdx)  + ") goto miniscript_statement_" + to_string(statement.statementIdx) + "; else" + "\n";
		}

		// enabled named conditions
		if (enabledNamedConditions.empty() == false) {
			generatedCode+= "\n";
			generatedCode+= methodIndent + "// enabled named conditions" + "\n";
			generatedCode+= methodIndent + "{" + "\n";
			generatedCode+= methodIndent + "\t" + "auto scriptIdxToStart = determineNamedScriptIdxToStart();" + "\n";
			generatedCode+= methodIndent + "\t" + "if (scriptIdxToStart != SCRIPTIDX_NONE && scriptIdxToStart != getScriptState().scriptIdx) {" + "\n";
			generatedCode+= methodIndent + "\t\t" + "resetScriptExecutationState(scriptIdxToStart, STATEMACHINESTATE_NEXT_STATEMENT);" + "\n";
			generatedCode+= methodIndent + "\t\t" + "timeEnabledConditionsCheckLast = _Time::getCurrentMillis();" + "\n";
			generatedCode+= methodIndent + "\t\t" + "return;" + "\n";
			generatedCode+= methodIndent + "\t" + "}" + "\n";
			generatedCode+= methodIndent + "}" + "\n";
		}

		// statement_xyz goto label
		generatedCode+= methodIndent + "// statement: " + to_string(statement.statementIdx) + "\n";
		if (scriptStateChanged == true || gotoStatementIdxSet.find(statement.statementIdx) != gotoStatementIdxSet.end()) {
			generatedCode+= methodIndent + "miniscript_statement_" + to_string(statement.statementIdx) + ":" + "\n";
		}
		scriptStateChanged = false;
		auto scriptStopped = false;
		transpileStatement(
			miniScript,
			generatedCode,
			syntaxTree,
			statement,
			MiniScript::SCRIPTIDX_NONE,
			scriptIdx,
			statementIdx,
			methodCodeMap,
			allMethods,
			scriptStateChanged,
			scriptStopped,
			enabledNamedConditions
		);
		if (scriptStopped == true) {
			generatedCode+= methodIndent + "if (getScriptState().running == false) {" + "\n";
			generatedCode+= methodIndent + "\t" + "MINISCRIPT_METHOD_POPSTACK();" + "\n";
			generatedCode+= methodIndent + "\t" + "return;" + "\n";
			generatedCode+= methodIndent + "}" + "\n";
		}
		if (scriptStateChanged == true) {
			generatedCode+= methodIndent + "if (getScriptState().state != STATEMACHINESTATE_NEXT_STATEMENT) {" + "\n";
			generatedCode+= methodIndent + "\t" + "getScriptState().statementIdx++;" + "\n";
			generatedCode+= methodIndent + "\t" + "return;" + "\n";
			generatedCode+= methodIndent + "}" + "\n";
		}
	}
	generatedCode+= methodIndent + "getScriptState().scriptIdx = SCRIPTIDX_NONE;" + "\n";
	generatedCode+= methodIndent + "getScriptState().statementIdx = STATEMENTIDX_NONE;" + "\n";
	generatedCode+= methodIndent + "setScriptStateState(STATEMACHINESTATE_WAIT_FOR_CONDITION);" + "\n";

	//
	generatedCodeHeader+= methodIndent + "if (miniScriptGotoStatementIdx != STATEMENTIDX_NONE && miniScriptGotoStatementIdx != STATEMENTIDX_FIRST) _Console::printLine(\"Transpiler::" + methodName + "(): Can not go to statement \" + to_string(miniScriptGotoStatementIdx));" + "\n";
	//
	generatedCode = generatedCodeHeader + generatedCode;
	return true;
}

bool Transpiler::transpileCondition(MiniScript* miniScript, string& generatedCode, int scriptIdx, const unordered_map<string, vector<string>>& methodCodeMap, const unordered_set<string>& allMethods, const string& returnValue, const string& injectCode, int depth) {
	if (scriptIdx < 0 || scriptIdx >= miniScript->getScripts().size()) {
		Console::printLine("Transpiler::transpileScriptCondition(): invalid script index");
		return false;
	}

	//
	const auto& script = miniScript->getScripts()[scriptIdx];

	//
	Console::printLine("Transpiler::transpileScriptCondition(): transpiling code condition for condition = '" + script.condition + "', with name '" + script.name + "'");

	//
	auto statementIdx = MiniScript::STATEMENTIDX_FIRST;
	auto scriptStateChanged = false;
	auto scriptStopped = false;
	vector<string >enabledNamedConditions;
	transpileStatement(
		miniScript,
		generatedCode,
		script.conditionSyntaxTree,
		script.conditionStatement,
		scriptIdx,
		MiniScript::SCRIPTIDX_NONE,
		statementIdx,
		methodCodeMap,
		allMethods,
		scriptStateChanged,
		scriptStopped,
		enabledNamedConditions,
		0,
		{},
		returnValue,
		injectCode,
		depth + 1
	);

	//
	generatedCode+= "\t\n";

	//
	return true;
}

const string Transpiler::createSourceCode(const MiniScript::SyntaxTreeNode& syntaxTreeNode) {
	//
	string result;
	switch (syntaxTreeNode.type) {
		case MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_LITERAL:
			{
				switch(syntaxTreeNode.value.getType()) {
					case MiniScript::TYPE_NULL:
						{
							result+= (result.empty() == false?", ":"") + string("<VOID>");
							break;
						}
					case MiniScript::TYPE_BOOLEAN:
					case MiniScript::TYPE_INTEGER:
					case MiniScript::TYPE_FLOAT:
						{
							result+= (result.empty() == false?", ":"") + syntaxTreeNode.value.getValueAsString();
							break;
						}
					case MiniScript::TYPE_STRING:
						{
							result+= (result.empty() == false?", ":"") + string("\"") + syntaxTreeNode.value.getValueAsString() + string("\"");
							break;
						}
					default:
						{
							result+= (result.empty() == false?", ":"") + string("<COMPLEX DATATYPE>");
							break;
						}
				}
				break;
			}
		case MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_EXECUTE_METHOD:
		case MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_EXECUTE_FUNCTION:
		case MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_EXECUTE_STACKLET:
			{
				auto endElse = syntaxTreeNode.value.getValueAsString() == "end" || syntaxTreeNode.value.getValueAsString() == "else";
				result+= syntaxTreeNode.value.getValueAsString();
				if (endElse == false) result+= string("(");
				auto argumentIdx = 0;
				for (const auto& argument: syntaxTreeNode.arguments) {
					if (argumentIdx > 0) result+= ", ";
					result+= createSourceCode(argument);
					argumentIdx++;
				}
				if (endElse == false) result+= string(")");
				break;
			}
		default:
			break;
	}
	return result;
}

const string Transpiler::createSourceCode(MiniScript::Script::Type scriptType, const string& condition, const vector<MiniScript::Script::Argument>& functionArguments, const string& name, const MiniScript::SyntaxTreeNode& conditionSyntaxTree, const vector<MiniScript::SyntaxTreeNode>& syntaxTree) {
	//
	string result;
	//
	switch(scriptType) {
		case MiniScript::Script::TYPE_FUNCTION: {
			result+= "function: ";
			if (condition.empty() == false) {
				result+= condition;
			}
			auto argumentIdx = 0;
			result+= "(";
			for (const auto& argument: functionArguments) {
				if (argumentIdx > 0) result+= ", ";
				if (argument.reference == true) result+= "=";
				result+= argument.name;
				argumentIdx++;
			}
			result+= ")";
			break;
		}
		case MiniScript::Script::TYPE_ON:
			{
				result+= "on: ";
				if (condition.empty() == false) {
					result+= condition;
				}
				break;
			}
		case MiniScript::Script::TYPE_ONENABLED:
			{
				result+= "on-enabled: ";
				if (condition.empty() == false) {
					result+= condition;
				}
				break;
			}
		default: break;
	}
	if (conditionSyntaxTree.type != MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_NONE)
		result+= createSourceCode(conditionSyntaxTree);
	if (name.empty() == false) {
		result+= " := " + name + "\n";
	} else {
		result+= "\n";
	}
	//
	auto indent = 1;
	for (const auto& syntaxTreeNode: syntaxTree) {
		if (syntaxTreeNode.type == MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_EXECUTE_METHOD) {
			if (syntaxTreeNode.value.getValueAsString() == "if") indent+= 0; else
			if (syntaxTreeNode.value.getValueAsString() == "elseif") indent-= 1; else
			if (syntaxTreeNode.value.getValueAsString() == "else") indent-= 1; else
			if (syntaxTreeNode.value.getValueAsString() == "end") indent-= 1; else
			if (syntaxTreeNode.value.getValueAsString() == "forTime") indent-= 0; else
			if (syntaxTreeNode.value.getValueAsString() == "forCondition") indent-= 0; else
			if (syntaxTreeNode.value.getValueAsString() == "switch") indent-= 0; else
			if (syntaxTreeNode.value.getValueAsString() == "case") indent-= 1; else
			if (syntaxTreeNode.value.getValueAsString() == "default") indent-= 1;
		}
		for (auto i = 0; i < indent; i++) result+= "\t";
		result+= createSourceCode(syntaxTreeNode) + "\n";
		if (syntaxTreeNode.type == MiniScript::SyntaxTreeNode::SCRIPTSYNTAXTREENODE_EXECUTE_METHOD) {
			if (syntaxTreeNode.value.getValueAsString() == "if") indent+= 1; else
			if (syntaxTreeNode.value.getValueAsString() == "elseif") indent+= 1; else
			if (syntaxTreeNode.value.getValueAsString() == "else") indent+= 1; else
			if (syntaxTreeNode.value.getValueAsString() == "end") indent+= 0; else
			if (syntaxTreeNode.value.getValueAsString() == "forTime") indent+= 1; else
			if (syntaxTreeNode.value.getValueAsString() == "forCondition") indent+= 1; else
			if (syntaxTreeNode.value.getValueAsString() == "switch") indent+= 0; else
			if (syntaxTreeNode.value.getValueAsString() == "case") indent+= 1; else
			if (syntaxTreeNode.value.getValueAsString() == "default") indent+= 1;
		}
	}
	return result;
}

const string Transpiler::createSourceCode(MiniScript* miniScript) {
	string result;
	// create source code
	for (const auto& script: miniScript->getScripts()) {
		result+= createSourceCode(script.type, script.emitCondition == true?script.condition:string(), script.arguments, script.name, script.conditionSyntaxTree, script.syntaxTree);
	}
	//
	return result;
}
