#include <span>
#include <unordered_map>
#include <vector>

#include <minitscript/minitscript.h>
#include <minitscript/minitscript/BaseMethods.h>
#include <minitscript/minitscript/MinitScript.h>
#include <minitscript/utilities/Console.h>
#include <minitscript/utilities/Hex.h>
#include <minitscript/utilities/Time.h>

using std::span;
using std::unordered_map;
using std::vector;

using minitscript::minitscript::BaseMethods;

using minitscript::minitscript::MinitScript;

using _Console = minitscript::utilities::Console;
using _Hex = minitscript::utilities::Hex;
using _Time = minitscript::utilities::Time;

void BaseMethods::registerConstants(MinitScript* minitScript) {
	minitScript->setConstant("$___NULL", MinitScript::Variable());
	minitScript->setConstant("$___ARRAY", MinitScript::Variable(vector<MinitScript::Variable*>()));
	minitScript->setConstant("$___MAP", MinitScript::Variable(unordered_map<string, MinitScript::Variable*>()));
}

void BaseMethods::registerMethods(MinitScript* minitScript) {
	// script internal base methods
	{
		//
		class MethodInternalScriptEvaluate: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodInternalScriptEvaluate(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_PSEUDO_MIXED, .name = "statement", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_PSEUDO_MIXED
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "internal.script.evaluate";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				if (arguments.size() != 1) {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				} else
				if (arguments.size() == 1) {
					returnValue.setValue(arguments[0]);
				}
			}
			bool isPrivate() const override {
				return true;
			}
		};
		minitScript->registerMethod(new MethodInternalScriptEvaluate(minitScript));
	}
	{
		//
		class MethodInternalEvaluateMemberAccess: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodInternalEvaluateMemberAccess(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_STRING, .name = "variable", .optional = false, .reference = false, .nullable = true },
						{ .type = MinitScript::TYPE_PSEUDO_MIXED, .name = "this", .optional = false, .reference = false, .nullable = true },
						{ .type = MinitScript::TYPE_STRING, .name = "member", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_PSEUDO_MIXED,
					true
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "internal.script.evaluateMemberAccess";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				// Current layout:
				//	0: variable name of object, 1: variable content of object
				//	2: object method to call
				//	3: variable name of argument 0; 4: variable content of argument 0
				//	5: variable name of argument 1; 6: variable content of argument 1
				//	..
				//
				string variable;
				string member;
				//
				if (arguments.size() < 3 ||
					minitScript->getStringValue(arguments, 2, member, false) == false) {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				} else {
					// do we have a this variable name?
					{
						string thisVariableName;
						if (arguments[0].getType() != MinitScript::TYPE_NULL && arguments[0].getStringValue(thisVariableName) == true) {
							// yep, looks like that, we always use a reference here
							#if defined(__MINITSCRIPT_TRANSPILATION__)
								arguments[1] = MinitScript::Variable::createReferenceVariable(&EVALUATEMEMBERACCESS_ARGUMENT0);
							#else
								arguments[1] = minitScript->getVariable(thisVariableName, &statement, true);
							#endif
						}
					}
					// check if map, if so fetch function assignment of member property
					auto functionScriptIdx = MinitScript::SCRIPTIDX_NONE;
					if (arguments[1].getType() == MinitScript::TYPE_MAP) {
						string function;
						auto mapValue = arguments[1].getMapEntry(member);
						if (mapValue.getType() == MinitScript::TYPE_FUNCTION_ASSIGNMENT && mapValue.getFunctionValue(function, functionScriptIdx) == true) {
							if (functionScriptIdx == MinitScript::SCRIPTIDX_NONE) functionScriptIdx = minitScript->getFunctionScriptIdx(function);
						}
					}
					//
					const auto& className = arguments[1].getTypeAsString();
					//
					if (className.empty() == false || functionScriptIdx != MinitScript::SCRIPTIDX_NONE) {
						//
						MinitScript::Method* method { nullptr };
						if (functionScriptIdx == MinitScript::SCRIPTIDX_NONE) {
							#if defined(__MINITSCRIPT_TRANSPILATION__)
								method = evaluateMemberAccessArrays[static_cast<int>(arguments[1].getType()) - static_cast<int>(MinitScript::TYPE_STRING)][EVALUATEMEMBERACCESS_MEMBER];
							#else
								method = minitScript->getMethod(className + "::" + member);
							#endif
						}
						if (method != nullptr || functionScriptIdx != MinitScript::SCRIPTIDX_NONE) {
							// create method call arguments
							vector<MinitScript::Variable> callArguments(1 + (arguments.size() - 3) / 2);
							//	this
							callArguments[0] = move(arguments[1]);
							//	additional method call arguments
							{
								auto callArgumentIdx = 1;
								for (auto argumentIdx = 3; argumentIdx < arguments.size(); argumentIdx+=2) {
									// do we have a this variable name?
									string argumentVariableName;
									if (arguments[argumentIdx].getType() != MinitScript::TYPE_NULL && arguments[argumentIdx].getStringValue(argumentVariableName) == true) {
										#if defined(__MINITSCRIPT_TRANSPILATION__)
											if (method != nullptr) {
												arguments[argumentIdx + 1] =
													callArgumentIdx >= method->getArgumentTypes().size() || method->getArgumentTypes()[callArgumentIdx].reference == false?
														MinitScript::Variable::createNonReferenceVariable(&EVALUATEMEMBERACCESS_ARGUMENTS[callArgumentIdx - 1]):
														EVALUATEMEMBERACCESS_ARGUMENTS[callArgumentIdx - 1];
											} else
											if (functionScriptIdx != MinitScript::SCRIPTIDX_NONE) {
												arguments[argumentIdx + 1] =
													callArgumentIdx >= minitScript->getScripts()[functionScriptIdx].arguments.size() || minitScript->getScripts()[functionScriptIdx].arguments[callArgumentIdx].reference == false?
														MinitScript::Variable::createNonReferenceVariable(&EVALUATEMEMBERACCESS_ARGUMENTS[callArgumentIdx - 1]):
														EVALUATEMEMBERACCESS_ARGUMENTS[callArgumentIdx - 1];
											}
										#else
											// yep, looks like that
											if (method != nullptr) {
												arguments[argumentIdx + 1] = minitScript->getVariable(argumentVariableName, &statement, callArgumentIdx >= method->getArgumentTypes().size()?false:method->getArgumentTypes()[callArgumentIdx].reference);
											} else
											if (functionScriptIdx != MinitScript::SCRIPTIDX_NONE) {
												arguments[argumentIdx + 1] = minitScript->getVariable(argumentVariableName, &statement, callArgumentIdx >= minitScript->getScripts()[functionScriptIdx].arguments.size()?false:minitScript->getScripts()[functionScriptIdx].arguments[callArgumentIdx].reference);
											}
										#endif
									}
									//
									callArguments[callArgumentIdx] = move(arguments[argumentIdx + 1]);
									callArgumentIdx++;
								}
							}
							//
							span callArgumentsSpan(callArguments);
							//
							if (method != nullptr) {
								method->executeMethod(callArgumentsSpan, returnValue, statement);
							} else
							if (functionScriptIdx != MinitScript::SCRIPTIDX_NONE) {
								minitScript->call(functionScriptIdx, callArgumentsSpan, returnValue);
							}
							// write back arguments from call arguments
							//	this
							arguments[1] = move(callArgumentsSpan[0]);
							//	additional arguments
							{
								auto callArgumentIdx = 1;
								for (auto argumentIdx = 3; argumentIdx < arguments.size(); argumentIdx+=2) {
									arguments[argumentIdx] = move(callArgumentsSpan[callArgumentIdx]);
									callArgumentIdx++;
								}
							}
						} else {
							MINITSCRIPT_METHODUSAGE_COMPLAINM(getMethodName(), "Class/object member not found: " + member + "()");
						}
					} else {
						MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
					}
				}
			}
			bool isVariadic() const override {
				return true;
			}
			bool isPrivate() const override {
				return true;
			}
		};
		minitScript->registerMethod(new MethodInternalEvaluateMemberAccess(minitScript));
	}
	{
		//
		class MethodInternalScriptCallStacklet: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodInternalScriptCallStacklet(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_STACKLET_ASSIGNMENT, .name = "stacklet", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_PSEUDO_MIXED
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "internal.script.callStacklet";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				string stacklet;
				auto stackletScriptIdx = MinitScript::SCRIPTIDX_NONE;
				if (arguments.size() == 1 &&
					minitScript->getStackletValue(arguments, 0, stacklet, stackletScriptIdx) == true) {
					if (stackletScriptIdx == MinitScript::SCRIPTIDX_NONE) {
						stackletScriptIdx = minitScript->getFunctionScriptIdx(stacklet);
					}
					if (stackletScriptIdx == MinitScript::SCRIPTIDX_NONE || minitScript->getScripts()[stackletScriptIdx].type != MinitScript::Script::TYPE_STACKLET) {
						MINITSCRIPT_METHODUSAGE_COMPLAINM(getMethodName(), "Stacklet not found: " + stacklet);
					} else {
						vector<MinitScript::Variable> callArguments(0);
						span callArgumentsSpan(callArguments);
						minitScript->callStacklet(stackletScriptIdx, callArgumentsSpan, returnValue);
					}
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
			bool isPrivate() const override {
				return true;
			}
		};
		minitScript->registerMethod(new MethodInternalScriptCallStacklet(minitScript));
	}
	// base methods
	{
		//
		class MethodReturn: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodReturn(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_PSEUDO_MIXED, .name = "value", .optional = true, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_NULL
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "return";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				if (arguments.size() == 0 || arguments.size() == 1) {
					if (minitScript->isFunctionRunning() == false) {
						MINITSCRIPT_METHODUSAGE_COMPLAINM(getMethodName(), "No function is being executed, return($value) has no effect");
					} else
					if (arguments.size() == 1) minitScript->getScriptState().returnValue.setValue(arguments[0]);
					minitScript->stopRunning();
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
		};
		minitScript->registerMethod(new MethodReturn(minitScript));
	}
	{
		//
		class MethodBreak: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodBreak(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_INTEGER, .name = "levels", .optional = true, .reference = false, .nullable = false }
					}
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "break";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				int64_t levels = 1;
				if ((arguments.size() == 0 || arguments.size() == 1) &&
					MinitScript::getIntegerValue(arguments, 0, levels, true) == true) {
					if (minitScript->getScriptState().blockStack.empty() == true) {
						MINITSCRIPT_METHODUSAGE_COMPLAINM(getMethodName(), "break without forCondition/forTime");
					} else {
						int64_t level = 0;
						auto& blockStack = minitScript->getScriptState().blockStack;
						MinitScript::ScriptState::Block* endType = nullptr;
						vector<int> blockStacksToRemove;
						for (int i = blockStack.size() - 1; i >= 0; i--) {
							if (blockStack[i].type == MinitScript::ScriptState::Block::TYPE_FOR) {
								endType = &blockStack[i];
								level++;
								blockStacksToRemove.push_back(i);
								if (level == levels) break;
							} else
							if (level < levels) {
								blockStacksToRemove.push_back(i);
							}
						}
						if (endType == nullptr) {
							MINITSCRIPT_METHODUSAGE_COMPLAINM(getMethodName(), "break without forCondition, forTime");
						} else
						if (levels != level) {
							MINITSCRIPT_METHODUSAGE_COMPLAINM(getMethodName(), "break(" + to_string(levels) + ") + without " + to_string(levels) + " levels of forCondition, forTime");
						} else
						if (endType->breakStatement != nullptr) {
							auto breakStatement = endType->breakStatement;
							for (auto i: blockStacksToRemove) blockStack.erase(blockStack.begin() + i);
							minitScript->gotoStatement(*breakStatement);
						} else {
							MINITSCRIPT_METHODUSAGE_COMPLAINM(getMethodName(), "No break statement");
						}
					}
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
		};
		minitScript->registerMethod(new MethodBreak(minitScript));
	}
	{
		//
		class MethodContinue: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodContinue(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_INTEGER, .name = "levels", .optional = true, .reference = false, .nullable = false }
					}
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "continue";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				int64_t levels = 1;
				if ((arguments.size() == 0 || arguments.size() == 1) &&
					MinitScript::getIntegerValue(arguments, 0, levels, true) == true) {
					if (minitScript->getScriptState().blockStack.empty() == true) {
						MINITSCRIPT_METHODUSAGE_COMPLAINM(getMethodName(), "continue without forCondition, forTime");
					} else {
						int64_t level = 0;
						auto& blockStack = minitScript->getScriptState().blockStack;
						MinitScript::ScriptState::Block* endType = nullptr;
						vector<int> blockStacksToRemove;
						for (int i = blockStack.size() - 1; i >= 0; i--) {
							if (blockStack[i].type == MinitScript::ScriptState::Block::TYPE_FOR) {
								endType = &blockStack[i];
								level++;
								if (level == levels) {
									break;
								} else {
									blockStacksToRemove.push_back(i);
								}
							} else
							if (level < levels) {
								blockStacksToRemove.push_back(i);
							}
						}
						if (endType == nullptr) {
							MINITSCRIPT_METHODUSAGE_COMPLAINM(getMethodName(), "continue without forCondition, forTime");
						} else
						if (levels != level) {
							MINITSCRIPT_METHODUSAGE_COMPLAINM(getMethodName(), "continue(" + to_string(levels) + ") + without " + to_string(levels) + " levels of forCondition, forTime");
						} else
						if (endType->continueStatement != nullptr) {
							auto continueStatement = endType->continueStatement;
							for (auto i: blockStacksToRemove) blockStack.erase(blockStack.begin() + i);
							minitScript->gotoStatement(*continueStatement);
						} else {
							MINITSCRIPT_METHODUSAGE_COMPLAINM(getMethodName(), "No continue statement");
						}
					}
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
		};
		minitScript->registerMethod(new MethodContinue(minitScript));
	}
	{
		//
		class MethodEnd: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodEnd(MinitScript* minitScript): MinitScript::Method(), minitScript(minitScript) {}
			const string getMethodName() override {
				return "end";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				if (arguments.size() == 0) {
					if (minitScript->getScriptState().blockStack.empty() == true) {
						MINITSCRIPT_METHODUSAGE_COMPLAINM(getMethodName(), "end without if/elseif/else/switch/case/default/forCondition/forTime/end");
					} else {
						auto& blockStack = minitScript->getScriptState().blockStack;
						auto& block = blockStack.back();
						if (block.type == MinitScript::ScriptState::Block::TYPE_FUNCTION || block.type == MinitScript::ScriptState::Block::TYPE_STACKLET) {
							minitScript->stopRunning();
						} else
						if (block.type ==  MinitScript::ScriptState::Block::TYPE_FOR && block.parameter.getType() == MinitScript::TYPE_INTEGER) {
							vector<MinitScript::Variable> arguments {};
							span argumentsSpan(arguments);
							MinitScript::Variable returnValue;
							int64_t iterationStackletScriptIdx;
							if (block.parameter.getIntegerValue(iterationStackletScriptIdx) == true &&
								iterationStackletScriptIdx != MinitScript::SCRIPTIDX_NONE) {
								minitScript->callStacklet(iterationStackletScriptIdx, argumentsSpan, returnValue);
							} else {
								MINITSCRIPT_METHODUSAGE_COMPLAINM(getMethodName(), "end with for: iteration stacklet: invalid stacklet");
							}
						}
						blockStack.erase(blockStack.begin() + blockStack.size() - 1);
						if (minitScript->hasEmitted() == false &&
							statement.gotoStatementIdx != MinitScript::STATEMENTIDX_NONE) {
							minitScript->gotoStatementGoto(statement);
						}
					}
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
		};
		minitScript->registerMethod(new MethodEnd(minitScript));
	}
	{
		//
		class MethodForTime: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodForTime(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_INTEGER, .name = "time", .optional = false, .reference = false, .nullable = false }
					}
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "forTime";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				int64_t time;
				if (arguments.size() == 1 &&
					minitScript->getIntegerValue(arguments, 0, time) == true) {
					auto& scriptState = minitScript->getScriptState();
					auto now = _Time::getCurrentMillis();
					auto timeWaitStarted = now;
					auto forTimeStartedIt = scriptState.forTimeStarted.find(statement.line);
					if (forTimeStartedIt == scriptState.forTimeStarted.end()) {
						minitScript->getScriptState().forTimeStarted[statement.line] = timeWaitStarted;
					} else {
						timeWaitStarted = forTimeStartedIt->second;
					}
					//
					if (_Time::getCurrentMillis() > timeWaitStarted + time) {
						scriptState.forTimeStarted.erase(statement.line);
						minitScript->gotoStatementGoto(statement);
					} else {
						scriptState.blockStack.emplace_back(
							MinitScript::ScriptState::Block::TYPE_FORTIME,
							false,
							&minitScript->getScripts()[scriptState.scriptIdx].statements[statement.gotoStatementIdx - 1],
							&minitScript->getScripts()[scriptState.scriptIdx].statements[statement.gotoStatementIdx],
							MinitScript::Variable()
						);
					}
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
		};
		minitScript->registerMethod(new MethodForTime(minitScript));
	}
	{
		//
		class MethodForCondition: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodForCondition(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_BOOLEAN, .name = "condition", .optional = false, .reference = false, .nullable = false },
						{ .type = MinitScript::TYPE_STACKLET_ASSIGNMENT, .name = "iterationStacklet", .optional = true, .reference = false, .nullable = false }
					}
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "forCondition";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				bool booleanValue;
				string iterationStacklet;
				auto iterationStackletScriptIdx = MinitScript::SCRIPTIDX_NONE;
				if ((arguments.size() == 1 || arguments.size() == 2) &&
					minitScript->getBooleanValue(arguments, 0, booleanValue) == true &&
					minitScript->getStackletValue(arguments, 1, iterationStacklet, iterationStackletScriptIdx, true) == true) {
					if (booleanValue == false) {
						minitScript->gotoStatementGoto(statement);
					} else {
						if (iterationStacklet.empty() == false && iterationStackletScriptIdx == MinitScript::SCRIPTIDX_NONE) {
							iterationStackletScriptIdx = minitScript->getFunctionScriptIdx(iterationStacklet);
						}
						// check if valid
						if (iterationStacklet.empty() == false && iterationStackletScriptIdx == MinitScript::SCRIPTIDX_NONE) {
							MINITSCRIPT_METHODUSAGE_COMPLAINM(getMethodName(), "Stacklet not found: " + iterationStacklet);
						} else {
							auto& scriptState = minitScript->getScriptState();
							scriptState.blockStack.emplace_back(
								MinitScript::ScriptState::Block::TYPE_FOR,
								false,
								&minitScript->getScripts()[scriptState.scriptIdx].statements[statement.gotoStatementIdx - 1],
								&minitScript->getScripts()[scriptState.scriptIdx].statements[statement.gotoStatementIdx],
								iterationStacklet.empty() == true?MinitScript::Variable():MinitScript::Variable(static_cast<int64_t>(iterationStackletScriptIdx))
							);
						}
					}
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
		};
		minitScript->registerMethod(new MethodForCondition(minitScript));
	}
	{
		//
		class MethodIfCondition: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodIfCondition(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_BOOLEAN, .name = "condition", .optional = false, .reference = false, .nullable = false }
					}
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "if";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				bool booleanValue;
				if (arguments.size() == 1 &&
					minitScript->getBooleanValue(arguments, 0, booleanValue) == true) {
					minitScript->getScriptState().blockStack.emplace_back(MinitScript::ScriptState::Block::TYPE_IF, booleanValue, nullptr, nullptr, MinitScript::Variable());
					if (booleanValue == false) {
						minitScript->gotoStatementGoto(statement);
					}
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
		};
		minitScript->registerMethod(new MethodIfCondition(minitScript));
	}
	{
		//
		class MethodElseIfCondition: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodElseIfCondition(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_BOOLEAN, .name = "condition", .optional = false, .reference = false, .nullable = false }
					}
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "elseif";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				bool booleanValue;
				if (arguments.size() == 1 &&
					minitScript->getBooleanValue(arguments, 0, booleanValue) == true) {
					auto& scriptState = minitScript->getScriptState();
					if (minitScript->getScriptState().blockStack.empty() == true) {
						MINITSCRIPT_METHODUSAGE_COMPLAINM(getMethodName(), "elseif without if");
					} else {
						auto& block = scriptState.blockStack.back();
						if (block.type != MinitScript::ScriptState::Block::TYPE_IF) {
							MINITSCRIPT_METHODUSAGE_COMPLAINM(getMethodName(), "elseif without if");
						} else
						if (block.match == true || booleanValue == false) {
							minitScript->gotoStatementGoto(statement);
						} else {
							block.match = booleanValue;
						}
					}
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
		};
		minitScript->registerMethod(new MethodElseIfCondition(minitScript));
	}
	{
		//
		class MethodElse: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodElse(MinitScript* minitScript): MinitScript::Method(), minitScript(minitScript) {}
			const string getMethodName() override {
				return "else";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				if (arguments.size() == 0) {
					auto& scriptState = minitScript->getScriptState();
					auto& block = scriptState.blockStack.back();
					if (block.type != MinitScript::ScriptState::Block::TYPE_IF) {
						MINITSCRIPT_METHODUSAGE_COMPLAINM(getMethodName(), "else without if");
					} else
					if (block.match == true) {
						minitScript->gotoStatementGoto(statement);
					}
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
		};
		minitScript->registerMethod(new MethodElse(minitScript));
	}
	// switch
	{
		//
		class MethodSwitch: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodSwitch(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_PSEUDO_MIXED, .name = "value", .optional = false, .reference = false, .nullable = false }
					}
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "switch";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				if (arguments.size() == 1) {
					auto& scriptState = minitScript->getScriptState();
					scriptState.blockStack.emplace_back(MinitScript::ScriptState::Block::TYPE_SWITCH, false, nullptr, nullptr, arguments[0]);
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
		};
		minitScript->registerMethod(new MethodSwitch(minitScript));
	}
	{
		//
		class MethodCase: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodCase(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_PSEUDO_MIXED, .name = "value", .optional = false, .reference = false, .nullable = false }
					}
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "case";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				if (arguments.size() == 1) {
					auto& scriptState = minitScript->getScriptState();
					if (minitScript->getScriptState().blockStack.empty() == true) {
						MINITSCRIPT_METHODUSAGE_COMPLAINM(getMethodName(), "case without switch");
					} else {
						auto& block = scriptState.blockStack.back();
						if (block.type != MinitScript::ScriptState::Block::TYPE_SWITCH) {
							MINITSCRIPT_METHODUSAGE_COMPLAINM(getMethodName(), "case without switch");
						} else {
							auto match = arguments[0].getValueAsString() == block.parameter.getValueAsString();
							if (block.match == true || match == false) {
								minitScript->gotoStatementGoto(statement);
							} else {
								block.match = match;
								scriptState.blockStack.emplace_back(MinitScript::ScriptState::Block::TYPE_CASE, false, nullptr, nullptr, MinitScript::Variable());
							}
						}
					}
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
		};
		minitScript->registerMethod(new MethodCase(minitScript));
	}
	{
		//
		class MethodDefault: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodDefault(MinitScript* minitScript): MinitScript::Method(), minitScript(minitScript) {}
			const string getMethodName() override {
				return "default";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				if (arguments.size() == 0) {
					auto& scriptState = minitScript->getScriptState();
					if (minitScript->getScriptState().blockStack.empty() == true) {
						MINITSCRIPT_METHODUSAGE_COMPLAINM(getMethodName(), "default without switch");
					} else {
						auto& block = scriptState.blockStack.back();
						if (block.type != MinitScript::ScriptState::Block::TYPE_SWITCH) {
							MINITSCRIPT_METHODUSAGE_COMPLAINM(getMethodName(), "default without switch");
						} else
						if (block.match == true) {
							minitScript->gotoStatementGoto(statement);
						}
					}
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
		};
		minitScript->registerMethod(new MethodDefault(minitScript));
	}
	// equality
	{
		//
		class MethodEquals: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodEquals(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_PSEUDO_MIXED, .name = "a", .optional = false, .reference = false, .nullable = false },
						{ .type = MinitScript::TYPE_PSEUDO_MIXED, .name = "b", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_BOOLEAN
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "equals";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				if (arguments.size() == 2) {
					returnValue.setValue(true);
					for (auto i = 1; i < arguments.size(); i++) {
						if (arguments[0].getValueAsString() != arguments[i].getValueAsString()) {
							returnValue.setValue(false);
							break;
						}
					}
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
			MinitScript::Operator getOperator() const override {
				return MinitScript::OPERATOR_EQUALS;
			}
		};
		minitScript->registerMethod(new MethodEquals(minitScript));
	}
	{
		//
		class MethodNotEqual: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodNotEqual(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_PSEUDO_MIXED, .name = "a", .optional = false, .reference = false, .nullable = false },
						{ .type = MinitScript::TYPE_PSEUDO_MIXED, .name = "b", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_BOOLEAN
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "notEqual";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				if (arguments.size() == 2) {
					returnValue.setValue(true);
					for (auto i = 1; i < arguments.size(); i++) {
						if (arguments[0].getValueAsString() == arguments[i].getValueAsString()) {
							returnValue.setValue(false);
							break;
						}
					}
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
			MinitScript::Operator getOperator() const override {
				return MinitScript::OPERATOR_NOTEQUAL;
			}
		};
		minitScript->registerMethod(new MethodNotEqual(minitScript));
	}
	// int methods
	{
		//
		class MethodInt: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodInt(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_INTEGER, .name = "integer", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_INTEGER
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "integer";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				int64_t integerValue;
				if (arguments.size() == 1 &&
					MinitScript::getIntegerValue(arguments, 0, integerValue) == true) {
					returnValue.setValue(integerValue);
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
		};
		minitScript->registerMethod(new MethodInt(minitScript));
	}
	// float methods
	//	TODO: move me into FloatMethods
	{
		//
		class MethodFloat: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodFloat(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_FLOAT, .name = "float", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_FLOAT
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "float";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				float floatValue;
				if (arguments.size() == 1 &&
					MinitScript::getFloatValue(arguments, 0, floatValue) == true) {
					returnValue.setValue(floatValue);
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
		};
		minitScript->registerMethod(new MethodFloat(minitScript));
	}
	{
		//
		class MethodFloatToIntValue: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodFloatToIntValue(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_FLOAT, .name = "float", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_INTEGER
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "float.toIntegerValue";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				float floatValue;
				if (arguments.size() == 1 &&
					MinitScript::getFloatValue(arguments, 0, floatValue) == true) {
					returnValue.setValue(static_cast<int64_t>(*((uint32_t*)&floatValue)));
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
		};
		minitScript->registerMethod(new MethodFloatToIntValue(minitScript));
	}
	{
		//
		class MethodFloatfromIntValue: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodFloatfromIntValue(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_INTEGER, .name = "integer", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_FLOAT
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "float.fromIntegerValue";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				int64_t intValue;
				if (arguments.size() == 1 &&
					MinitScript::getIntegerValue(arguments, 0, intValue) == true) {
					returnValue.setValue(*((float*)&intValue));
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
		};
		minitScript->registerMethod(new MethodFloatfromIntValue(minitScript));
	}
	//
	{
		//
		class MethodGreater: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodGreater(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_PSEUDO_MIXED, .name = "a", .optional = false, .reference = false, .nullable = false },
						{ .type = MinitScript::TYPE_PSEUDO_MIXED, .name = "b", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_BOOLEAN),
					minitScript(minitScript) {}
			const string getMethodName() override {
				return "greater";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				if (arguments.size() == 2) {
					if (MinitScript::hasType(arguments, MinitScript::TYPE_STRING) == true) {
						string stringValueA;
						string stringValueB;
						if (MinitScript::getStringValue(arguments, 0, stringValueA, false) == true &&
							MinitScript::getStringValue(arguments, 1, stringValueB, false) == true) {
							returnValue.setValue(stringValueA > stringValueB);
						} else {
							MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
						}
					} else {
						float floatValueA;
						float floatValueB;
						if (MinitScript::getFloatValue(arguments, 0, floatValueA, false) == true &&
							MinitScript::getFloatValue(arguments, 1, floatValueB, false) == true) {
							returnValue.setValue(floatValueA > floatValueB);
						} else {
							MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
						}
					}
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
			MinitScript::Operator getOperator() const override {
				return MinitScript::OPERATOR_GREATER;
			}
		};
		minitScript->registerMethod(new MethodGreater(minitScript));
	}
	{
		//
		class MethodGreaterEquals: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodGreaterEquals(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_PSEUDO_MIXED, .name = "a", .optional = false, .reference = false, .nullable = false },
						{ .type = MinitScript::TYPE_PSEUDO_MIXED, .name = "b", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_BOOLEAN),
					minitScript(minitScript) {}
			const string getMethodName() override {
				return "greaterEquals";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				if (arguments.size() == 2) {
					if (MinitScript::hasType(arguments, MinitScript::TYPE_STRING) == true) {
						string stringValueA;
						string stringValueB;
						if (MinitScript::getStringValue(arguments, 0, stringValueA, false) == true &&
							MinitScript::getStringValue(arguments, 1, stringValueB, false) == true) {
							returnValue.setValue(stringValueA >= stringValueB);
						} else {
							MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
						}
					} else {
						float floatValueA;
						float floatValueB;
						if (MinitScript::getFloatValue(arguments, 0, floatValueA, false) == true &&
							MinitScript::getFloatValue(arguments, 1, floatValueB, false) == true) {
							returnValue.setValue(floatValueA >= floatValueB);
						} else {
							MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
						}
					}
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
			MinitScript::Operator getOperator() const override {
				return MinitScript::OPERATOR_GREATEREQUALS;
			}
		};
		minitScript->registerMethod(new MethodGreaterEquals(minitScript));
	}
	{
		//
		class MethodLesser: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodLesser(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_PSEUDO_MIXED, .name = "a", .optional = false, .reference = false, .nullable = false },
						{ .type = MinitScript::TYPE_PSEUDO_MIXED, .name = "b", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_BOOLEAN),
					minitScript(minitScript) {}
			const string getMethodName() override {
				return "lesser";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				if (arguments.size() == 2) {
					if (MinitScript::hasType(arguments, MinitScript::TYPE_STRING) == true) {
						string stringValueA;
						string stringValueB;
						if (MinitScript::getStringValue(arguments, 0, stringValueA, false) == true &&
							MinitScript::getStringValue(arguments, 1, stringValueB, false) == true) {
							returnValue.setValue(stringValueA < stringValueB);
						} else {
							MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
						}
					} else {
						float floatValueA;
						float floatValueB;
						if (MinitScript::getFloatValue(arguments, 0, floatValueA, false) == true &&
							MinitScript::getFloatValue(arguments, 1, floatValueB, false) == true) {
							returnValue.setValue(floatValueA < floatValueB);
						} else {
							MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
						}
					}
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
			MinitScript::Operator getOperator() const override {
				return MinitScript::OPERATOR_LESSER;
			}
		};
		minitScript->registerMethod(new MethodLesser(minitScript));
	}
	{
		//
		class MethodLesserEquals: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodLesserEquals(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_PSEUDO_MIXED, .name = "a", .optional = false, .reference = false, .nullable = false },
						{ .type = MinitScript::TYPE_PSEUDO_MIXED, .name = "b", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_BOOLEAN),
					minitScript(minitScript) {}
			const string getMethodName() override {
				return "lesserEquals";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				if (arguments.size() == 2) {
					if (MinitScript::hasType(arguments, MinitScript::TYPE_STRING) == true) {
						string stringValueA;
						string stringValueB;
						if (MinitScript::getStringValue(arguments, 0, stringValueA, false) == true &&
							MinitScript::getStringValue(arguments, 1, stringValueB, false) == true) {
							returnValue.setValue(stringValueA <= stringValueB);
						} else {
							MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
						}
					} else {
						float floatValueA;
						float floatValueB;
						if (MinitScript::getFloatValue(arguments, 0, floatValueA, false) == true &&
							MinitScript::getFloatValue(arguments, 1, floatValueB, false) == true) {
							returnValue.setValue(floatValueA <= floatValueB);
						} else {
							MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
						}
					}
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
			MinitScript::Operator getOperator() const override {
				return MinitScript::OPERATOR_LESSEREQUALS;
			}
		};
		minitScript->registerMethod(new MethodLesserEquals(minitScript));
	}
	// bool methods
	{
		//
		class MethodBool: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodBool(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_BOOLEAN, .name = "boolean", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_BOOLEAN
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "boolean";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				bool booleanValue;
				if (arguments.size() == 1 &&
					MinitScript::getBooleanValue(arguments, 0, booleanValue) == true) {
					returnValue.setValue(booleanValue);
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
		};
		minitScript->registerMethod(new MethodBool(minitScript));
	}
	{
		//
		class MethodNot: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodNot(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_BOOLEAN, .name = "boolean", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_BOOLEAN), minitScript(minitScript) {}
			const string getMethodName() override {
				return "not";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				bool booleanValue = false;
				if (arguments.size() == 1 &&
					MinitScript::getBooleanValue(arguments, 0, booleanValue) == true) {
					returnValue.setValue(!booleanValue);
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
			MinitScript::Operator getOperator() const override {
				return MinitScript::OPERATOR_NOT;
			}
		};
		minitScript->registerMethod(new MethodNot(minitScript));
	}
	{
		//
		class MethodAnd: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodAnd(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_BOOLEAN, .name = "a", .optional = false, .reference = false, .nullable = false },
						{ .type = MinitScript::TYPE_BOOLEAN, .name = "b", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_BOOLEAN
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "and";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				bool booleanValueA;
				bool booleanValueB;
				if (arguments.size() == 2 &&
					MinitScript::getBooleanValue(arguments, 0, booleanValueA) == true &&
					MinitScript::getBooleanValue(arguments, 1, booleanValueB) == true) {
					returnValue.setValue(booleanValueA && booleanValueB);
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
			MinitScript::Operator getOperator() const override {
				return MinitScript::OPERATOR_AND;
			}
		};
		minitScript->registerMethod(new MethodAnd(minitScript));
	}
	{
		//
		class MethodOr: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodOr(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_BOOLEAN, .name = "a", .optional = false, .reference = false, .nullable = false },
						{ .type = MinitScript::TYPE_BOOLEAN, .name = "b", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_BOOLEAN
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "or";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				bool booleanValueA;
				bool booleanValueB;
				if (arguments.size() == 2 &&
					MinitScript::getBooleanValue(arguments, 0, booleanValueA) == true &&
					MinitScript::getBooleanValue(arguments, 1, booleanValueB) == true) {
					returnValue.setValue(booleanValueA || booleanValueB);
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
			MinitScript::Operator getOperator() const override {
				return MinitScript::OPERATOR_OR;
			}
		};
		minitScript->registerMethod(new MethodOr(minitScript));
	}
	// get type
	{
		//
		class MethodGetType: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodGetType(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_PSEUDO_MIXED, .name = "variable", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_STRING
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "getType";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				if (arguments.size() == 1) {
					returnValue.setValue(arguments[0].getTypeAsString());
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
		};
		minitScript->registerMethod(new MethodGetType(minitScript));
	}
	// has variable
	{
		//
		class MethodHasVariable: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodHasVariable(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_STRING, .name = "variable", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_BOOLEAN
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "hasVariable";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				string variable;
				if (arguments.size() == 1 &&
					MinitScript::getStringValue(arguments, 0, variable) == true) {
					returnValue.setValue(minitScript->hasVariable(variable, &statement));
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
		};
		minitScript->registerMethod(new MethodHasVariable(minitScript));
	}
	// get variable
	{
		//
		class MethodGetVariable: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodGetVariable(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_STRING, .name = "variable", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_PSEUDO_MIXED
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "getVariable";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				string variable;
				if (arguments.size() == 1 &&
					MinitScript::getStringValue(arguments, 0, variable) == true) {
					returnValue.setValue(minitScript->getVariable(variable, &statement));
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
		};
		minitScript->registerMethod(new MethodGetVariable(minitScript));
	}
	// get variable for method arguments
	{
		//
		class MethodGetMethodArgumentVariable: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodGetMethodArgumentVariable(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_STRING, .name = "variable", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_PSEUDO_MIXED
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "getMethodArgumentVariable";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				string variable;
				if (arguments.size() == 1 &&
					MinitScript::getStringValue(arguments, 0, variable) == true) {
					returnValue = minitScript->getMethodArgumentVariable(variable, &statement);
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
			bool isPrivate() const override {
				return true;
			}
		};
		minitScript->registerMethod(new MethodGetMethodArgumentVariable(minitScript));
	}
	// get variable reference
	{
		//
		class MethodGetVariableReference: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodGetVariableReference(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_STRING, .name = "variable", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_PSEUDO_MIXED
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "getVariableReference";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				string variable;
				if (arguments.size() == 1 &&
					MinitScript::getStringValue(arguments, 0, variable) == true) {
					returnValue = minitScript->getVariable(variable, &statement, true);
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
			bool isPrivate() const override {
				return true;
			}
		};
		minitScript->registerMethod(new MethodGetVariableReference(minitScript));
	}
	// set variable
	{
		//
		class MethodSetVariable: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodSetVariable(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_STRING, .name = "variable", .optional = false, .reference = false, .nullable = false },
						{ .type = MinitScript::TYPE_PSEUDO_MIXED, .name = "value", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_PSEUDO_MIXED
				),
				minitScript(minitScript) {
				//
			}
			const string getMethodName() override {
				return "setVariable";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				string variable;
				if (arguments.size() == 2 &&
					MinitScript::getStringValue(arguments, 0, variable) == true) {
					minitScript->setVariable(variable, arguments[1].isConstant() == true?MinitScript::Variable::createNonConstVariable(&arguments[1]):arguments[1], &statement);
					returnValue.setValue(arguments[1]);
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
			MinitScript::Operator getOperator() const override {
				return MinitScript::OPERATOR_SET;
			}
		};
		minitScript->registerMethod(new MethodSetVariable(minitScript));
	}
	// set variable reference
	{
		//
		class MethodSetVariableReference: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodSetVariableReference(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_STRING, .name = "variable", .optional = false, .reference = false, .nullable = false },
						{ .type = MinitScript::TYPE_PSEUDO_MIXED, .name = "value", .optional = false, .reference = true, .nullable = false }
					},
					MinitScript::TYPE_PSEUDO_MIXED
				),
				minitScript(minitScript) {
				//
			}
			const string getMethodName() override {
				return "setVariableReference";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				string variable;
				if (arguments.size() == 2 &&
					MinitScript::getStringValue(arguments, 0, variable) == true) {
					minitScript->setVariable(variable, arguments[1], &statement, true);
					returnValue.setValue(arguments[1]);
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
			bool isPrivate() const override {
				return true;
			}
		};
		minitScript->registerMethod(new MethodSetVariableReference(minitScript));
	}
	// Unset variable reference
	{
		//
		class MethodUnsetVariableReference: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodUnsetVariableReference(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_STRING, .name = "variable", .optional = false, .reference = false, .nullable = false }
					}
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "unsetVariableReference";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				string variable;
				if (arguments.size() == 1 &&
					MinitScript::getStringValue(arguments, 0, variable) == true) {
					minitScript->unsetVariable(variable, &statement);
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
			bool isPrivate() const override {
				return true;
			}
		};
		minitScript->registerMethod(new MethodUnsetVariableReference(minitScript));
	}
	// set constant
	{
		//
		class MethodSetConstant: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodSetConstant(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_STRING, .name = "constant", .optional = false, .reference = false, .nullable = false },
						{ .type = MinitScript::TYPE_PSEUDO_MIXED, .name = "value", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_PSEUDO_MIXED
				),
				minitScript(minitScript) {
				//
			}
			const string getMethodName() override {
				return "setConstant";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				string constant;
				if (arguments.size() == 2 &&
					MinitScript::getStringValue(arguments, 0, constant) == true) {
					MinitScript::setConstant(arguments[1]);
					minitScript->setVariable(constant, arguments[1], &statement);
					returnValue.setValue(arguments[1]);
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
		};
		minitScript->registerMethod(new MethodSetConstant(minitScript));
	}
	{
		//
		class MethodPostfixIncrement: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodPostfixIncrement(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_INTEGER, .name = "variable", .optional = false, .reference = true, .nullable = false },
					},
					MinitScript::TYPE_INTEGER
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "postfixIncrement";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				int64_t value;
				if (arguments.size() == 1 &&
					MinitScript::getIntegerValue(arguments, 0, value) == true) {
					arguments[0].setValue(value + 1);
					returnValue.setValue(value);
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
			MinitScript::Operator getOperator() const override {
				return MinitScript::OPERATOR_POSTFIX_INCREMENT;
			}
		};
		minitScript->registerMethod(new MethodPostfixIncrement(minitScript));
	}
	{
		//
		class MethodPostfixDecrement: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodPostfixDecrement(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_INTEGER, .name = "variable", .optional = false, .reference = true, .nullable = false },
					},
					MinitScript::TYPE_INTEGER
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "postfixDecrement";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				int64_t value;
				if (arguments.size() == 1 &&
					MinitScript::getIntegerValue(arguments, 0, value) == true) {
					arguments[0].setValue(value - 1);
					returnValue.setValue(value);
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
			MinitScript::Operator getOperator() const override {
				return MinitScript::OPERATOR_POSTFIX_DECREMENT;
			}
		};
		minitScript->registerMethod(new MethodPostfixDecrement(minitScript));
	}
	{
		//
		class MethodPrefixIncrement: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodPrefixIncrement(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_INTEGER, .name = "variable", .optional = false, .reference = true, .nullable = false },
					},
					MinitScript::TYPE_INTEGER
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "prefixIncrement";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				int64_t value;
				if (arguments.size() == 1 &&
					MinitScript::getIntegerValue(arguments, 0, value) == true) {
					++value;
					arguments[0].setValue(value);
					returnValue.setValue(value);
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
			MinitScript::Operator getOperator() const override {
				return MinitScript::OPERATOR_PREFIX_INCREMENT;
			}
		};
		minitScript->registerMethod(new MethodPrefixIncrement(minitScript));
	}
	{
		//
		class MethodPrefixDecrement: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodPrefixDecrement(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_INTEGER, .name = "variable", .optional = false, .reference = true, .nullable = false },
					},
					MinitScript::TYPE_INTEGER
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "prefixDecrement";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				int64_t value;
				if (arguments.size() == 1 &&
					MinitScript::getIntegerValue(arguments, 0, value) == true) {
					--value;
					arguments[0].setValue(value);
					returnValue.setValue(value);
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
			MinitScript::Operator getOperator() const override {
				return MinitScript::OPERATOR_PREFIX_DECREMENT;
			}
		};
		minitScript->registerMethod(new MethodPrefixDecrement(minitScript));
	}
	//
	{
		//
		class MethodBitwiseNot: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodBitwiseNot(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_INTEGER, .name = "value", .optional = false, .reference = false, .nullable = false },
					},
					MinitScript::TYPE_INTEGER),
					minitScript(minitScript) {}
			const string getMethodName() override {
				return "bitwiseNot";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				int64_t value;
				if (arguments.size() == 1 &&
					MinitScript::getIntegerValue(arguments, 0, value) == true) {
					returnValue.setValue(~value);
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
			MinitScript::Operator getOperator() const override {
				return MinitScript::OPERATOR_BITWISENOT;
			}
		};
		minitScript->registerMethod(new MethodBitwiseNot(minitScript));
	}
	//
	{
		class MethodBitwiseAnd: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodBitwiseAnd(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_INTEGER, .name = "a", .optional = false, .reference = false, .nullable = false },
						{ .type = MinitScript::TYPE_INTEGER, .name = "b", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_INTEGER),
					minitScript(minitScript) {}
			const string getMethodName() override {
				return "bitwiseAnd";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				int64_t valueA;
				int64_t valueB;
				if (MinitScript::getIntegerValue(arguments, 0, valueA) == true &&
					MinitScript::getIntegerValue(arguments, 1, valueB) == true) {
					returnValue.setValue(valueA & valueB);
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
			MinitScript::Operator getOperator() const override {
				return MinitScript::OPERATOR_BITWISEAND;
			}
		};
		minitScript->registerMethod(new MethodBitwiseAnd(minitScript));
	}
	//
	{
		class MethodBitwiseOr: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodBitwiseOr(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_INTEGER, .name = "a", .optional = false, .reference = false, .nullable = false },
						{ .type = MinitScript::TYPE_INTEGER, .name = "b", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_INTEGER),
					minitScript(minitScript) {}
			const string getMethodName() override {
				return "bitwiseOr";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				int64_t valueA;
				int64_t valueB;
				if (arguments.size() == 2 &&
					MinitScript::getIntegerValue(arguments, 0, valueA) == true &&
					MinitScript::getIntegerValue(arguments, 1, valueB) == true) {
					returnValue.setValue(valueA | valueB);
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
			MinitScript::Operator getOperator() const override {
				return MinitScript::OPERATOR_BITWISEOR;
			}
		};
		minitScript->registerMethod(new MethodBitwiseOr(minitScript));
	}
	//
	{
		class MethodBitwiseXor: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodBitwiseXor(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_INTEGER, .name = "a", .optional = false, .reference = false, .nullable = false },
						{ .type = MinitScript::TYPE_INTEGER, .name = "b", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_INTEGER),
					minitScript(minitScript) {}
			const string getMethodName() override {
				return "bitwiseXor";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				int64_t valueA;
				int64_t valueB;
				if (arguments.size() == 2 &&
					MinitScript::getIntegerValue(arguments, 0, valueA) == true &&
					MinitScript::getIntegerValue(arguments, 1, valueB) == true) {
					returnValue.setValue(valueA ^ valueB);
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
			MinitScript::Operator getOperator() const override {
				return MinitScript::OPERATOR_BITWISEXOR;
			}
		};
		minitScript->registerMethod(new MethodBitwiseXor(minitScript));
	}
	// hex: move me into HexMethods
	{
		//
		class MethodHexEncode: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodHexEncode(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_INTEGER, .name = "value", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_STRING
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "hex.encode";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				int64_t value;
				if (arguments.size() == 1 &&
					MinitScript::getIntegerValue(arguments, 0, value) == true) {
					returnValue.setValue(_Hex::encodeInt(value));
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
		};
		minitScript->registerMethod(new MethodHexEncode(minitScript));
	}
	{
		//
		class MethodHexDecode: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			MethodHexDecode(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_STRING, .name = "value", .optional = false, .reference = false, .nullable = false }
					},
					MinitScript::TYPE_INTEGER
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "hex.decode";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				string value;
				if (arguments.size() == 1 &&
					MinitScript::getStringValue(arguments, 0, value) == true) {
					returnValue.setValue(static_cast<int64_t>(_Hex::decodeInt(value)));
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
		};
		minitScript->registerMethod(new MethodHexDecode(minitScript));
	}
	{
		//
		class SwapMethod: public MinitScript::Method {
		private:
			MinitScript* minitScript { nullptr };
		public:
			SwapMethod(MinitScript* minitScript):
				MinitScript::Method(
					{
						{ .type = MinitScript::TYPE_PSEUDO_MIXED, .name = "a", .optional = false, .reference = true, .nullable = false },
						{ .type = MinitScript::TYPE_PSEUDO_MIXED, .name = "b", .optional = false, .reference = true, .nullable = false }
					}
				),
				minitScript(minitScript) {}
			const string getMethodName() override {
				return "swap";
			}
			void executeMethod(span<MinitScript::Variable>& arguments, MinitScript::Variable& returnValue, const MinitScript::Statement& statement) override {
				if (arguments.size() == 2) {
					// TODO: improve me!!!
					auto tmp = MinitScript::Variable::createNonReferenceVariable(&arguments[0]);
					arguments[0].setValue(arguments[1]);
					arguments[1].setValue(tmp);
				} else {
					MINITSCRIPT_METHODUSAGE_COMPLAIN(getMethodName());
				}
			}
		};
		minitScript->registerMethod(new SwapMethod(minitScript));
	}
}
