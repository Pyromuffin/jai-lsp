using OmniSharp.Extensions.LanguageServer.Protocol;
using OmniSharp.Extensions.LanguageServer.Protocol.Client.Capabilities;
using OmniSharp.Extensions.LanguageServer.Protocol.Document;
using OmniSharp.Extensions.LanguageServer.Protocol.Models;

using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace jai_lsp
{
    internal class CompletionHandler : ICompletionHandler
    {

        private readonly DocumentSelector _documentSelector = new DocumentSelector(
            new DocumentFilter()
            {
                Pattern = "**/*.jai"
            }
        );

        private CompletionCapability _capability;

        public CompletionHandler()
        {
        }

        public CompletionRegistrationOptions GetRegistrationOptions()
        {
            return new CompletionRegistrationOptions
            {
                TriggerCharacters = new Container<string>("."),
                DocumentSelector = _documentSelector,
                ResolveProvider = false
            };
        }

        public async Task<CompletionList> Handle(CompletionParams request, CancellationToken cancellationToken)
        {
            var documentHash = Hash.StringHash(request.TextDocument.Uri.GetFileSystemPath());
            var pos = request.Position;
            var namesPtr = TreeSitter.GetCompletionItems(documentHash, pos.Line, pos.Character, (int)request.Context.TriggerKind);
            var names = System.Runtime.InteropServices.Marshal.PtrToStringAnsi(namesPtr);
            if (names == null)
                return new CompletionList();

            var nameList = names.Split(",");
            List<CompletionItem> items = new List<CompletionItem>(nameList.Length);
            
            foreach(var name in nameList )
            {
                var completion = new CompletionItem();
                completion.Label = name;
                items.Add(completion);
            }

            return new CompletionList(items);
        }

        public void SetCapability(CompletionCapability capability)
        {
            _capability = capability;
        }
    }
}
