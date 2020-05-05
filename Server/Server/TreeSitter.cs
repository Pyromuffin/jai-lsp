using OmniSharp.Extensions.LanguageServer.Protocol.Models.Proposals;
using System;
using System.Collections.Generic;
using System.Data;
using System.Runtime.InteropServices;
using System.Text;

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
        unsafe extern static public void GetTokens([MarshalAs(UnmanagedType.LPStr)] string code, out SemanticToken* tokens, out int count);

        [DllImport(dllpath)]
        extern static public int FreeTokens([In, Out] SemanticToken[] tokens);

    }
}
