#pragma once

#include <stack>

#include <minitscript/minitscript.h>
#include <minitscript/minitscript/MinitScript.h>
#include <minitscript/utilities/Console.h>

#include <{$base-class-header}>

using std::stack;

using _Console = minitscript::utilities::Console;
/**
 * MinitScript -> C++ transpilation of {$script}
 */
class {$class-name}: public {$base-class} {
public:
	// forbid class copy
	_FORBID_CLASS_COPY({$class-name})

	/**
	 * Public constructor
	 */
	{$class-name}();

	/*__MINITSCRIPT_TRANSPILEDMINITSCRIPTCODE_DECLARATIONS_START__*/
	/*__MINITSCRIPT_TRANSPILEDMINITSCRIPTCODE_DECLARATIONS_END__*/

};
