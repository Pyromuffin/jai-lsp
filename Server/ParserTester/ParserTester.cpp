// ParserTester.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <chrono>
#include <unordered_map>
#include "../Tree-sitter-jai-lib/TreeSitterJai.h"


extern "C"
{
    int Init();
	const char* GetCompletionItems(const char* code, int row, int col);
	long long GetTokens(Hash documentHash, SemanticToken** outTokens, int* count);
	const char* GetSyntax(const Hash& documentHash);
	static void CreateFileScope(FileScope* fileScope, const TSNode& node, GapBuffer* buffer, std::vector<Scope*>& scopeKing);
	static void CreateScope(FileScope* fileScope, TSNode& node, GapBuffer* buffer, std::vector<TSNode>& parameters, Type*& currentType, std::vector<Scope*>& scopeKing);
	long long CreateTree(const char* document, const char* code, int length);
	long long EditTree(Hash documentHash, const char* change, int startLine, int startCol, int endLine, int endCol, int contentLength, int rangeLength);
	long long UpdateTree(Hash documentHash);
	long long CreateTreeFromPath(const char* document, const char* moduleName);
}

const char* path = "C:\\Users\\pyrom\\Desktop\\jai\\how_to\\010_calling_procedures.jai";

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
	std::cout << GetSyntax(StringHash("tomato")) << "\n";

//	UpdateTreeIncremental("tomato", changedCode, 1, 5, 18, -1);
	std::cout << GetSyntax(StringHash("tomato")) << "\n";
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
	/*
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

	*/
}

void PrintTokens(Hash documentHash)
{
	SemanticToken* tokens;
	int count;

	GetTokens(documentHash, &tokens, &count);
	for (int i = 0; i < count; i++)
	{
		std::cout << (int)tokens[i].type << "\n";
	}
}


int main()
{
	/*
	while (true)
	{
		std::string s;
		std::cin >> s;
		assert(false);
		std::cout << s;
	}

	*/
	
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


	auto modulePath = "C:\\Users\\pyrom\\Desktop\\jai\\modules\\Basic\\module.jai";
	Hash documentHash = StringHash("Tomato");

	const char* test1 =
		"main :: () {\n"
		"foo : int;\n"
		"}\n\n"
		;

	CreateTree(modulePath, test1, strlen(test1));

}


