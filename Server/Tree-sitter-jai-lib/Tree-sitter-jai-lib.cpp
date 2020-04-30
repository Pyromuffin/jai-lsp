// Tree-sitter-jai-lib.cpp : Defines the functions for the static library.
//

#include "framework.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <tree_sitter/api.h>
//#include <parser.h>

#define export __declspec(dllexport)

extern "C"
{
	TSLanguage* tree_sitter_jai();
	TSLanguage* jaiLang;
	TSFieldId nameId;

	char names[1000];

	export int Init()
	{
		jaiLang = tree_sitter_jai();
		nameId = ts_language_field_id_for_name(jaiLang, "name", strlen("name"));
		return 69420;
	}

	export const char* Parse(const char* code)
	{
		TSParser* parser = ts_parser_new();

		// Set the parser's language 
		ts_parser_set_language(parser, tree_sitter_jai());

		// Build a syntax tree based on source code stored in a string.
		TSTree* tree = ts_parser_parse_string(
			parser,
			NULL,
			code,
			strlen(code));

		TSNode root_node = ts_tree_root_node(tree);
		//char* string = ts_node_string(root_node);

		//TSTreeCursor ts_tree_cursor_new(root_node);

		uint32_t error_offset;
		TSQueryError error_type;
		auto queryText = "(variable_decl name: (identifier) @decl_name)"
			"(implicit_variable_decl name: (identifier) @decl_name)"
			"(function_definition name: (identifier) @decl_name)"
			;

		auto var_decl_query = ts_query_new(
			jaiLang,
			queryText,
			strlen(queryText),
			&error_offset,
			&error_type
		);

		TSQueryCursor* queryCursor = ts_query_cursor_new();
		ts_query_cursor_exec(queryCursor, var_decl_query, root_node);

		TSQueryMatch match;
		uint32_t index;
		int cursor = 0;
		
		while (ts_query_cursor_next_capture(queryCursor, &match, &index))
		{
			auto node = match.captures[index].node;
			auto start = ts_node_start_byte(node);
			auto end = ts_node_end_byte(node);
			auto length = end - start;
			strncpy_s(&names[cursor], sizeof(names) - cursor, &code[start], length);
			cursor += length;
			names[cursor] = ',';
			cursor++;
		}
		cursor--;
		if (cursor < 0) cursor = 0;

		names[cursor] = '\0';

		ts_tree_delete(tree);
		ts_parser_delete(parser);
		
		return names;
	}


}