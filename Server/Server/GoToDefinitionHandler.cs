using MediatR;
using Microsoft.Extensions.Logging;
using OmniSharp.Extensions.LanguageServer.Protocol;
using OmniSharp.Extensions.LanguageServer.Protocol.Models;
using OmniSharp.Extensions.LanguageServer.Protocol.Server;
using System;
using System.Threading;
using System.Threading.Tasks;

namespace jai_lsp
{
    class GoToDefinitionHandler : DefinitionHandler
    {

        HashNamer namer;
        ILogger _logger;

        public GoToDefinitionHandler(ProgressManager progressManager, HashNamer namer, ILogger<GoToDefinitionHandler> logger) : base(new DefinitionRegistrationOptions()
        {
            DocumentSelector = DocumentSelector.ForLanguage("jai"),
            WorkDoneProgress = false
        }
            , progressManager)
        {
            
            this.namer = namer;
            _logger = logger;
        }


        /*
        public GoToDefinitionHandler(ILogger<SemanticTokens> logger) : base(new SemanticTokensRegistrationOptions()
        {
            DocumentSelector = DocumentSelector.ForLanguage("jai"),
            Legend = new SemanticTokensLegend()
            {
                TokenTypes = typeNames,
                TokenModifiers = modifierNames,
            },
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
        */

        public override Task<LocationOrLocationLinks> Handle(DefinitionParams request, CancellationToken cancellationToken)
        {
            var hash = Hash.StringHash(request.TextDocument.Uri.GetFileSystemPath());
            TreeSitter.FindDefinition(hash, request.Position.Line, request.Position.Character, out var defHash, out var row, out var col);
            
            if(defHash != 0)
            {
                var position = new Position(row, col);
                var range = new OmniSharp.Extensions.LanguageServer.Protocol.Models.Range(position, position);
                LocationLink link = new LocationLink();
                link.TargetUri = "file://" + namer.hashToName[defHash];
                link.TargetRange = range;
                LocationOrLocationLinks ll = new LocationOrLocationLinks(link);
                return Task.FromResult(ll);
            }

            return Task.FromResult(new LocationOrLocationLinks());
        }
    }
}
