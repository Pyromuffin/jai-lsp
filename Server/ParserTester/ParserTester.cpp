// ParserTester.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#define _CRT_SECURE_NO_WARNINGS
#include <iostream>

enum class TokenType
{
	Documentation,
	Label,
	Parameter,
	Variable,
	Macro,
	Property,
	Function,
	TypeParameter,
	Enum,
	Interface,
	Member,
	Struct,
	Class,
	Keyword,
	String,
	Number,
	Comment,
	Operator,
	Namespace,
	Type,
	Regexp,
};

struct SemanticToken
{
	int line;
	int col;
	int length;
	TokenType type;
	int modifier;
};


extern "C"
{
    int Init();
	const char* GetCompletionItems(const char* code, int row, int col);
}

const char* path = "C:\\Users\\pyrom\\Desktop\\jai\\how_to\\001_first.jai";

char buffer[10000];

int main()
{
	Init();

	auto file = fopen(path, "r");
	int i = 0;

	while (1) {
		auto c = fgetc(file);
		if (feof(file)) {
			break;
		}
		buffer[i++] = c;
	}

	buffer[i] = '\0';

	auto items = GetCompletionItems(buffer, 102, 0);

	std::cout << items;
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
