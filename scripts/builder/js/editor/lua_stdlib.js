/**
 * lua_stdlib.js - Built-in Lua standard library definitions for the LSP
 * Covers: base, string, table, math, and bit32/utf8 packages.
 */

(function (global) {
  'use strict';

  const LUA_STDLIB = {
    // Top-level global functions from Lua base library
    base: [
      { name: 'assert', signature: 'assert(v [, message])', return: 'any', doc: 'Raises an error if value is false or nil; otherwise returns all its arguments.' },
      { name: 'error', signature: 'error(message [, level])', return: 'void', doc: 'Terminates the last protected function called and returns message as the error object.' },
      { name: 'ipairs', signature: 'ipairs(t)', return: 'iterator', doc: 'Returns three values: an iterator function, the table t, and 0 for numerical index iteration.' },
      { name: 'pairs', signature: 'pairs(t)', return: 'iterator', doc: 'Iterates over all key-value pairs of table t.' },
      { name: 'next', signature: 'next(table [, index])', return: 'key, value', doc: 'Allows a program to traverse all fields of a table.' },
      { name: 'pcall', signature: 'pcall(f [, arg1, ...])', return: 'status, ...', doc: 'Calls function f with the given arguments in protected mode.' },
      { name: 'print', signature: 'print(...)', return: 'void', doc: 'Prints arguments formatted as strings to standard output.' },
      { name: 'select', signature: 'select(index, ...)', return: 'any', doc: 'If index is a number, returns all arguments after index; if "#", returns total argument count.' },
      { name: 'tonumber', signature: 'tonumber(e [, base])', return: 'number|nil', doc: 'Tries to convert its argument to a number.' },
      { name: 'tostring', signature: 'tostring(v)', return: 'string', doc: 'Converts any value to a human-readable string representation.' },
      { name: 'type', signature: 'type(v)', return: 'string', doc: 'Returns the type of its only argument: "nil", "number", "string", "boolean", "table", "function", etc.' }
    ],

    // Modules
    modules: {
      string: {
        doc: 'Lua standard string manipulation library',
        members: [
          { name: 'byte', signature: 'string.byte(s [, i [, j]])', return: 'integer, ...', doc: 'Returns the internal numeric codes of characters s[i], s[i+1], ..., s[j].' },
          { name: 'char', signature: 'string.char(...)', return: 'string', doc: 'Receives zero or more integers and returns a string of matching character codes.' },
          { name: 'find', signature: 'string.find(s, pattern [, init [, plain]])', return: 'start, end', doc: 'Looks for the first match of pattern in string s. Returns start and end indices or nil.' },
          { name: 'format', signature: 'string.format(formatstring, ...)', return: 'string', doc: 'Returns a formatted version of its variable number of arguments following the description given in its first argument.' },
          { name: 'gmatch', signature: 'string.gmatch(s, pattern)', return: 'iterator', doc: 'Returns an iterator function that produces next captures from pattern over string s.' },
          { name: 'gsub', signature: 'string.gsub(s, pattern, repl [, n])', return: 'string, count', doc: 'Returns a copy of s in which occurrences of pattern have been replaced by a substitution repl.' },
          { name: 'len', signature: 'string.len(s)', return: 'integer', doc: 'Receives a string and returns its length.' },
          { name: 'lower', signature: 'string.lower(s)', return: 'string', doc: 'Receives a string and returns a copy with all uppercase letters converted to lowercase.' },
          { name: 'match', signature: 'string.match(s, pattern [, init])', return: 'captures...', doc: 'Looks for the first match of pattern in string s and returns the captures.' },
          { name: 'pack', signature: 'string.pack(fmt, v1, v2, ...)', return: 'string', doc: 'Returns a binary string containing the values v1, v2, etc. serialized according to format string fmt.' },
          { name: 'packsize', signature: 'string.packsize(fmt)', return: 'integer', doc: 'Returns the size of a string resulting from string.pack with the given format.' },
          { name: 'rep', signature: 'string.rep(s, n [, sep])', return: 'string', doc: 'Returns a string that is the concatenation of n copies of string s separated by sep.' },
          { name: 'reverse', signature: 'string.reverse(s)', return: 'string', doc: 'Returns a string that is the string s reversed.' },
          { name: 'sub', signature: 'string.sub(s, i [, j])', return: 'string', doc: 'Returns the substring of s that starts at i and continues until j (1-based, negative counts from end).' },
          { name: 'unpack', signature: 'string.unpack(fmt, s [, pos])', return: 'values..., nextpos', doc: 'Returns the values packed in string s according to format string fmt.' },
          { name: 'upper', signature: 'string.upper(s)', return: 'string', doc: 'Receives a string and returns a copy with all lowercase letters converted to uppercase.' }
        ]
      },

      table: {
        doc: 'Lua standard table manipulation library',
        members: [
          { name: 'concat', signature: 'table.concat(list [, sep [, i [, j]]])', return: 'string', doc: 'Given a list where all elements are strings or numbers, returns the string list[i]..sep..list[i+1]...sep..list[j].' },
          { name: 'insert', signature: 'table.insert(list, [pos,] value)', return: 'void', doc: 'Inserts element value at position pos in list, shifting other elements up.' },
          { name: 'move', signature: 'table.move(a1, f, e, t [,a2])', return: 'table', doc: 'Moves elements from table a1 to table a2.' },
          { name: 'pack', signature: 'table.pack(...)', return: 'table', doc: 'Returns a new table with all arguments stored into keys 1, 2, etc. and field "n" with the total number of arguments.' },
          { name: 'remove', signature: 'table.remove(list [, pos])', return: 'any', doc: 'Removes from list the element at position pos, returning the value of the removed element.' },
          { name: 'sort', signature: 'table.sort(list [, comp])', return: 'void', doc: 'Sorts list elements in a given order, in-place, from list[1] to list[#list].' },
          { name: 'unpack', signature: 'table.unpack(list [, i [, j]])', return: 'elements...', doc: 'Returns the elements from the given table as multiple return values.' }
        ]
      },

      math: {
        doc: 'Lua standard mathematical library',
        members: [
          { name: 'abs', signature: 'math.abs(x)', return: 'number', doc: 'Returns the absolute value of x.' },
          { name: 'ceil', signature: 'math.ceil(x)', return: 'integer', doc: 'Returns the smallest integral value larger than or equal to x.' },
          { name: 'floor', signature: 'math.floor(x)', return: 'integer', doc: 'Returns the largest integral value smaller than or equal to x.' },
          { name: 'fmod', signature: 'math.fmod(x, y)', return: 'number', doc: 'Returns the remainder of the division of x by y that rounds the quotient towards zero.' },
          { name: 'max', signature: 'math.max(x, ...)', return: 'number', doc: 'Returns the argument with the maximum value.' },
          { name: 'min', signature: 'math.min(x, ...)', return: 'number', doc: 'Returns the argument with the minimum value.' },
          { name: 'random', signature: 'math.random([m [, n]])', return: 'number', doc: 'Returns pseudo-random numbers.' },
          { name: 'randomseed', signature: 'math.randomseed([x [, y]])', return: 'void', doc: 'Sets the seed for the pseudo-random generator.' },
          { name: 'sqrt', signature: 'math.sqrt(x)', return: 'number', doc: 'Returns the square root of x.' },
          { name: 'type', signature: 'math.type(x)', return: 'string', doc: 'Returns "integer" if x is an integer, "float" if it is a float, or nil if x is not a number.' },
          { name: 'tointeger', signature: 'math.tointeger(x)', return: 'integer|nil', doc: 'Converts x to an integer if possible.' },
          { name: 'huge', signature: 'math.huge', return: 'number', doc: 'A value larger than any other numerical value (+infinity).' },
          { name: 'maxinteger', signature: 'math.maxinteger', return: 'integer', doc: 'An integer with the maximum value for an integer.' },
          { name: 'mininteger', signature: 'math.mininteger', return: 'integer', doc: 'An integer with the minimum value for an integer.' },
          { name: 'pi', signature: 'math.pi', return: 'number', doc: 'The value of π (pi).' }
        ]
      }
    }
  };

  global.LUA_STDLIB = LUA_STDLIB;
})(window);
