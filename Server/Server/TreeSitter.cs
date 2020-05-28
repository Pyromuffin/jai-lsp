using System;
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
    unsafe struct Gap
    {
        byte* before;
        int beforeLength;
        byte* after;
        int afterLength;

    }


    class TreeSitter
    {
        const string dllpath = "tree-sitter-jai-wrapper.dll";

        [DllImport(dllpath)]
        extern static public int Init();
        [DllImport(dllpath)]
        extern static public IntPtr GetCompletionItems([MarshalAs(UnmanagedType.LPStr)] string code, int row, int col);


        public static string CoolParse(string code, int row, int col)
        {
            var ptr = GetCompletionItems(code, row, col);
            return Marshal.PtrToStringAnsi(ptr);
        }


        [DllImport(dllpath)]
        extern static public long CreateTreeFromPath([MarshalAs(UnmanagedType.LPStr)] string document, [MarshalAs(UnmanagedType.LPStr)] string moduleName);

        [DllImport(dllpath)]
        extern static public long CreateTree(ulong documentHash, [MarshalAs(UnmanagedType.LPStr)] string code, int length);

        [DllImport(dllpath)]
        extern static public long UpdateTree(ulong documentHash);

        [DllImport(dllpath)]
        extern static public long EditTree(ulong documentHash, [MarshalAs(UnmanagedType.LPStr)] string change, int startLine, int startCol, int endLine, int endCol, int contentLength, int rangeLength);

        [DllImport(dllpath)]
        extern static public long GetTokens(ulong documentHash, out IntPtr tokens, out int count);

        [DllImport(dllpath)]
        extern static public void FindDefinition(ulong documentName, int row, int col, out ulong outFileHash, out int outRow, out int outCol);
    }
}
