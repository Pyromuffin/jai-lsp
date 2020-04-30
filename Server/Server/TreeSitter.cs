using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace jai_lsp
{
    class TreeSitter
    {
        const string dllpath = "tree-sitter-jai-wrapper.dll";


        [DllImport(dllpath)]
        extern static public int Init();

        [DllImport(dllpath)]
        extern static public IntPtr Parse([MarshalAs(UnmanagedType.LPStr)] string code);

        public static string CoolParse(string code)
        {
            var ptr = Parse(code);
            return Marshal.PtrToStringAnsi(ptr);
        }
        
    }
}
