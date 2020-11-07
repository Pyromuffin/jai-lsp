// ParserTester.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <chrono>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include "../Tree-sitter-jai-lib/TreeSitterJai.h"


extern "C"
{
    int Init();
	const char* GetCompletionItems(const char* code, int row, int col);
	long long GetTokens(Hash documentHash, SemanticToken** outTokens, int* count);
	const char* GetSyntax(const Hash& documentHash);
	static void HandleVariableReference(TSNode& node, GapBuffer* buffer, std::vector<Scope*>& scopeKing, FileScope* fileScope, std::vector<TSNode>& unresolvedEntry, std::vector<int>& unresolvedTokenIndex);
	static void HandleUnresolvedReferences(std::vector<int>& unresolvedTokenIndex, std::vector<TSNode>& unresolvedEntry, GapBuffer* buffer, FileScope* fileScope);
	static void CreateFileScope(FileScope* fileScope, const TSNode& node, GapBuffer* buffer, std::vector<Scope*>& scopeKing);
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
	//std::cout << GetSyntax(StringHash("tomato")) << "\n";

   //UpdateTreeIncremental("tomato", changedCode, 1, 5, 18, -1);
	//std::cout << GetSyntax(StringHash("tomato")) << "\n";
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



void ParseModules(int tries)
{
	std::unordered_map<std::string, std::string> files;

	auto modulesPath = "C:\\Users\\pyrom\\Desktop\\jai\\modules\\";
	auto directories = std::filesystem::directory_iterator(modulesPath);
	for (auto dir : directories)
	{
		if (dir.is_directory())
		{
			auto path = dir.path();
			auto subdirs = std::filesystem::directory_iterator(dir);
			for (auto subdir : subdirs)
			{
				if (subdir.path().extension().compare(".jai") == 0)
				{
					std::ifstream t(subdir.path());
					t.seekg(0, std::ios::end);
					size_t size = t.tellg();
					std::string buffer(size, ' ');
					t.seekg(0);
					t.read(&buffer[0], size);
					files[subdir.path().string()] = buffer;
				}
			}
			
		}
	}


	for (int i = 0; i < tries; i++)
	{
		long long time = 0;

		std::vector<Timings> timingses;

		for (auto& kvp : files)
		{
			Timings t;
			time += CreateTree(kvp.first.c_str(), kvp.second.c_str(), kvp.second.length());
			timingses.push_back(t);
		}

		int bufferTime = 0;
		int parseTime = 0;
		int scopeTime = 0;

		for (auto& t : timingses)
		{
			bufferTime += t.bufferTime;
			parseTime += t.parseTime - t.bufferTime;
			scopeTime += t.scopeTime - t.parseTime;
		}

		std::cout << time << ",";
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
	auto code =
		"zebra :: struct { legs : int; } \n"
		"bob : zebra; \n"
		"bob.legs = 5; \n"
		;


		CreateTree("zebra", code, strlen(code));

	//ParseModules(30);

}


