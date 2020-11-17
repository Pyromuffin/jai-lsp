using MediatR;
using OmniSharp.Extensions.LanguageServer.Protocol.Models;
using OmniSharp.Extensions.LanguageServer.Protocol.Workspace;
using System.Threading;
using System.Threading.Tasks;

namespace jai_lsp
{
    class WorkspaceFolderChangeHandler : DidChangeWorkspaceFoldersHandler
    {

        WorkspaceFolderChangeHandler(object registrationOptions) : base(registrationOptions)
        {

        }

        public override Task<Unit> Handle(DidChangeWorkspaceFoldersParams request, CancellationToken cancellationToken)
        {
            return Unit.Task;
        }
    }
}
