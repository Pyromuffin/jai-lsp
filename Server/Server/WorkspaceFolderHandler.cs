using MediatR;
using OmniSharp.Extensions.LanguageServer.Protocol.Models;
using OmniSharp.Extensions.LanguageServer.Protocol.Server;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace jai_lsp
{
    class WorkspaceFolderChangeHandler : DidChangeWorkspaceFoldersHandler
    {
        public override Task<Unit> Handle(DidChangeWorkspaceFoldersParams request, CancellationToken cancellationToken)
        {
            return Unit.Task;
        }
    }
}
