// ParserTester.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <chrono>
#include <unordered_map>
#include "../Tree-sitter-jai-lib/GapBuffer.h"

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
	long long GetTokens(const char* document, SemanticToken** outTokens, int* count);
	const char* GetSyntax(const char* document);
	long long CreateTree(const char* document, const char* code, int length);
	long long UpdateTree(const char* document, const char* change, int line, int col, int contentLength, int replacementLength);
	GapBuffer* GetGapBuffer(const char* document);

}

const char* path = "C:\\Users\\pyrom\\Desktop\\jai\\modules\\Basic\\Print.jai";

char buffer[100000];

static void EditReplaceTest()
{
	const char* originalCode =
		"main :: () {\n"
		"foo :: int;\n"
		"}\n";

	const char* changedCode =
		"main :: () {\n"
		"foo : int;\n"
		"}\n";


	//UpdateTree("tomato", originalCode, strlen(originalCode));
	std::cout << GetSyntax("tomato") << "\n";

//	UpdateTreeIncremental("tomato", changedCode, 1, 5, 18, -1);
	std::cout << GetSyntax("tomato") << "\n";
}

static void SemanticTokensTest()
{
	/*
	SemanticToken* tokens;
	int count;
	auto items = GetCompletionItems(buffer, 102, 0);


	auto now = std::chrono::system_clock::now();
	GetTokens(buffer, i -1, &tokens, &count);
	auto then = std::chrono::system_clock::now();
	auto duration = then - now;
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration);

	std::cout << "tokens duration: " << ms.count() << '\n';


	for (int i = 0; i < count; i++)
	{
		std::cout << (int)tokens[i].type << '\n';
	}

	std::cout << items;
	*/
}
static void GapBufferHashingTest()
{
	const char* codeA =
		"main :: () {\n"
		"foo :: int;\n"
		"}\n";


	const char* codeB =
		"Zebra :: () {\n"
		"main();\n"
		"}\n";

	auto gb1 = GapBuffer(codeA, strlen(codeA));
	auto gb2 = GapBuffer(codeB, strlen(codeB));

	auto bv1 = buffer_view();
	bv1.buffer = &gb1;
	bv1.length = 4;
	bv1.start = 0;

	auto bv2 = buffer_view();
	bv2.buffer = &gb2;
	bv2.length = 4;
	bv2.start = 14;

	std::cout << bv1.Copy() << "\n";
	std::cout << bv2.Copy() << "\n";

	std::unordered_map<buffer_view, int> map;
	map[bv1] = 500;

	std::cout << map[bv1] << "\n";
	std::cout << map[bv2] << "\n";


}


static void GapBufferTest()
{
	const char* originalCode =
		"main :: () {\n"
		"foo :: int;\n"
		"}\n";


	CreateTree("tomato", originalCode, strlen(originalCode));
	std::cout << GetSyntax("tomato") << "\n";

	UpdateTree("tomato", "=", 1, 5, 1, 1);

	GetGapBuffer("tomato")->PrintContents();

	std::cout << GetSyntax("tomato") << "\n";
}


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
	
	std::cout << CreateTree("tomato", buffer, i) << "\n";
	std::cout << UpdateTree("tomato", "=", 100, 0, 1, 1) << "\n";
	SemanticToken* tokens;
	int count;
	GetTokens("tomato", &tokens, &count);

	for (int i = 0; i < count; i++)
	{
		std::cout << (int)tokens[i].type << "\n";
	}


	GapBufferHashingTest();

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
