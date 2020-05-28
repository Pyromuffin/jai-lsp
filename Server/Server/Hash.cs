using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace jai_lsp
{
    public static class Hash // hope this works!
    {
        public static ulong StringHash(string str)
        {
            ulong hash = 0xcbf29ce484222325UL;

            for (int i = 0; i < str.Length; i++)
            {
                hash = hash ^ str[i];
                hash = hash * 0x00000100000001B3UL;
            }

            return hash;
        }
    }
}
