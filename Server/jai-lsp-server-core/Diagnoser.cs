using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using OmniSharp.Extensions.LanguageServer.Protocol;
using OmniSharp.Extensions.LanguageServer.Protocol.Document;
using OmniSharp.Extensions.LanguageServer.Protocol.Models;
using OmniSharp.Extensions.LanguageServer.Protocol.Server;

namespace jai_lsp
{

    public class Diagnoser
    {
        public ILanguageServer server;

        // does this need to be double concurrent? I don't know.
        ConcurrentDictionary<DocumentUri, ConcurrentDictionary<int, List<Diagnostic>>> diagnosticCategories = new ConcurrentDictionary<DocumentUri, ConcurrentDictionary<int, List<Diagnostic>>>();

        public void Add(DocumentUri document, int errorCategory, List<Diagnostic> diagnostics)
        {
            if (!diagnosticCategories.ContainsKey(document))
            {
                diagnosticCategories[document] = new ConcurrentDictionary<int, List<Diagnostic>>();
            }

            var documentErrors = diagnosticCategories[document];
            documentErrors[errorCategory] = diagnostics;
        }

        public void Publish(DocumentUri document)
        {
            PublishDiagnosticsParams diagnosticsParams = new PublishDiagnosticsParams();
            diagnosticsParams.Uri = document;

            var documentErrors = diagnosticCategories[document];
            var allErrors = documentErrors.Values.SelectMany(x => x);
            diagnosticsParams.Diagnostics = new Container<Diagnostic>(allErrors);
            server.PublishDiagnostics(diagnosticsParams);
        }

    }
}
