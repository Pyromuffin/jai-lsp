﻿using System;
using System.Runtime.InteropServices;

namespace jai_lsp
{
    [StructLayout(LayoutKind.Sequential)]
    struct SemanticToken
    {
        public int line;
        public int col;
        public int length;
        public TokenType type;
        public TokenModifier modifier;
    };

    [StructLayout(LayoutKind.Sequential)]
    struct Range
    {
        public int startLine;
        public int startCol;
        public int endLine;
        public int endCol;
    };

    [StructLayout(LayoutKind.Sequential)]
    unsafe struct Gap
    {
        byte* before;
        int beforeLength;
        byte* after;
        int afterLength;
    };


    class TreeSitter
    {
        const string dllpath = "tree-sitter-jai-wrapper.dll";

        [DllImport(dllpath)]
        extern static public int Init();
        [DllImport(dllpath)]
        extern static public IntPtr GetCompletionItems(ulong documentHash, int row, int col, int InvocationType);

        public static string GetSyntax(ulong documentHash)
        {
            var ptr = GetSyntaxNice(documentHash);
            return Marshal.PtrToStringAnsi(ptr);
        }

        [DllImport(dllpath)]
        extern static public long CreateTreeFromPath([MarshalAs(UnmanagedType.LPStr)] string document, [MarshalAs(UnmanagedType.LPStr)] string moduleName);

        [DllImport(dllpath)]
        extern static public long CreateTree([MarshalAs(UnmanagedType.LPStr)] string document, [MarshalAs(UnmanagedType.LPStr)] string code, int length);

        [DllImport(dllpath)]
        extern static public long UpdateTree(ulong documentHash);

        [DllImport(dllpath)]
        extern static public long EditTree(ulong documentHash, [MarshalAs(UnmanagedType.LPStr)] string change, int startLine, int startCol, int endLine, int endCol, int contentLength, int rangeLength);

        [DllImport(dllpath)]
        extern static public long GetTokens(ulong documentHash, out IntPtr tokens, out int count);

        [DllImport(dllpath)]
        extern static public void FindDefinition(ulong documentName, int row, int col, out ulong outFileHash, out Range origin, out Range target, out Range selection);

        [DllImport(dllpath)]
        extern static public IntPtr GetSyntaxNice(ulong documentHash);

        [DllImport(dllpath)]
        extern static public IntPtr Hover(ulong documentName, int row, int col);

        [DllImport(dllpath)]
        extern static public IntPtr GetLine(ulong documentName, int row);

        [DllImport(dllpath)]
        extern static public void GetSignature(ulong hashValue, int row, int col, out IntPtr signature, out int parameterCount, out int activeParameter, out int parameterErrorCount, out IntPtr parameterErrorRanges);

        [DllImport(dllpath)]
        extern static public void RegisterModule([MarshalAs(UnmanagedType.LPStr)] string document, [MarshalAs(UnmanagedType.LPStr)] string moduleName);

        [DllImport(dllpath)]
        extern static public void AddModuleDirectory([MarshalAs(UnmanagedType.LPStr)] string moduleDirectory);
    }
}
