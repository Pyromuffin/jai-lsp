using MediatR;
using Microsoft.Extensions.Logging;
using OmniSharp.Extensions.LanguageServer.Protocol;
using OmniSharp.Extensions.LanguageServer.Protocol.Client.Capabilities;
using OmniSharp.Extensions.LanguageServer.Protocol.Document.Server.Proposals;
using OmniSharp.Extensions.LanguageServer.Protocol.Models;
using OmniSharp.Extensions.LanguageServer.Protocol.Models.Proposals;
using System;
using System.Threading;
using System.Threading.Tasks;

#pragma warning disable 618

namespace jai_lsp
{
    class SemanticHighlight : SemanticTokensHandler
    {
        ILogger _logger;

        public SemanticHighlight(ILogger<SemanticTokens> logger) : base(new SemanticTokensRegistrationOptions()
        {
            DocumentSelector = DocumentSelector.ForLanguage("jai"),
            Legend = new SemanticTokensLegend(),
            DocumentProvider = new Supports<SemanticTokensDocumentProviderOptions>(true,
                  new SemanticTokensDocumentProviderOptions()
                  {
                      Edits = false
                  }),
            RangeProvider = false
        })
        {
            _logger = logger;
        }

        protected override Task<SemanticTokensDocument> GetSemanticTokensDocument(ITextDocumentIdentifierParams @params, CancellationToken cancellationToken)
        {
            return Task.FromResult(new SemanticTokensDocument(GetRegistrationOptions().Legend));
        }

        protected override Task Tokenize(SemanticTokensBuilder builder, ITextDocumentIdentifierParams identifier, CancellationToken cancellationToken)
        {

            builder.Push(1, 0, 10, 1, 1);
            return Unit.Task;
        }
    }
}
