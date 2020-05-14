using OmniSharp.Extensions.LanguageServer.Protocol.Client.Capabilities;
using OmniSharp.Extensions.LanguageServer.Protocol.Models;
using OmniSharp.Extensions.LanguageServer.Protocol.Server;
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
                DocumentSelector = _documentSelector,
                ResolveProvider = false
            };
        }

        public async Task<CompletionList> Handle(CompletionParams request, CancellationToken cancellationToken)
        {
            /*
            //_router.Window.LogInfo("Received Completion Request!");
       
            var documentPath = request.TextDocument.Uri.ToString();
            var text = _bufferManager.GetBuffer(documentPath);

            if (text == null)
            {
                return new CompletionList();
            }
            var pos = request.Position;
            var names = TreeSitter.CoolParse(text, pos.Line, pos.Character);
            var nameList = names.Split(",");
            List<CompletionItem> items = new List<CompletionItem>(nameList.Length);
            
            foreach(var name in nameList )
            {
                var completion = new CompletionItem();
                completion.Label = name;
                items.Add(completion);
            }

            return new CompletionList(items);
            */
            return new CompletionList();
        }

        public void SetCapability(CompletionCapability capability)
        {
            _capability = capability;
        }
    }
}
