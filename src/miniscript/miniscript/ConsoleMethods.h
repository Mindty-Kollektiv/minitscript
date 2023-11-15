#pragma once

#include <miniscript/miniscript.h>
#include <miniscript/utilities/fwd-miniscript.h>

using miniscript::miniscript::MiniScript;

/**
 * MiniScript console functions
 * @author Andreas Drewke
 */
class miniscript::miniscript::ConsoleMethods {
public:
	/**
	 * Register methods
	 * @param miniScript miniScript
	 */
	static void registerMethods(MiniScript* miniScript);

};