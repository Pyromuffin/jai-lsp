using MediatR;
using Microsoft.Extensions.Logging;
using OmniSharp.Extensions.LanguageServer.Protocol;
using OmniSharp.Extensions.LanguageServer.Protocol.Client.Capabilities;
using OmniSharp.Extensions.LanguageServer.Protocol.Models;
using OmniSharp.Extensions.LanguageServer.Protocol.Server;
using System;
using System.Threading;
using System.Threading.Tasks;

namespace jai_lsp
{
    class Definer: IDefinitionHandler
    {
        HashNamer hashNamer;
        ILogger _logger;

        private readonly DocumentSelector _documentSelector = new DocumentSelector(
            new DocumentFilter()
            {
                Pattern = "**/*.jai"
            }
        );


        public Definer(ILogger<TextDocumentHandler> logger, HashNamer hashNamer)
        {
            _logger = logger;
            this.hashNamer = hashNamer;
        }

        public DefinitionRegistrationOptions GetRegistrationOptions()
        {
            var options = new DefinitionRegistrationOptions();
            options.DocumentSelector = _documentSelector;
            options.WorkDoneProgress = false;
            return options;
        }

        OmniSharp.Extensions.LanguageServer.Protocol.Models.Range ConvertRange(Range range)
        {
            var r = new OmniSharp.Extensions.LanguageServer.Protocol.Models.Range();
            r.Start = new Position();
            r.End = new Position();

            r.Start.Character = range.startCol;
            r.Start.Line = range.startLine;
            r.End.Character = range.endCol;
            r.End.Line = range.endLine;

            return r;
        }

        public Task<LocationOrLocationLinks> Handle(DefinitionParams request, CancellationToken cancellationToken)
        {
            var hash = Hash.StringHash(request.TextDocument.Uri.GetFileSystemPath());
            TreeSitter.FindDefinition(hash, request.Position.Line, request.Position.Character, out var defHash, out var origin, out var target, out var selection);

            if (defHash != 0)
            {
                LocationLink link = new LocationLink();
                link.TargetUri = DocumentUri.FromFileSystemPath(hashNamer.hashToName[defHash]);
                link.OriginSelectionRange = ConvertRange(origin);
                link.TargetRange = ConvertRange(target);
                link.TargetSelectionRange = ConvertRange(selection);
                LocationOrLocationLinks ll = new LocationOrLocationLinks(link);
                return Task.FromResult(ll);
            }

            return Task.FromResult(new LocationOrLocationLinks());
        }

        public void SetCapability(DefinitionCapability capability)
        {
        }
    }


    class Hoverer : IHoverHandler
    {

        private readonly DocumentSelector _documentSelector = new DocumentSelector(
            new DocumentFilter()
            {
                Pattern = "**/*.jai"
            }
        );

        public HoverRegistrationOptions GetRegistrationOptions()
        {
            var options = new HoverRegistrationOptions();
            options.DocumentSelector = _documentSelector;
            options.WorkDoneProgress = false;
            return options;
        }

        public Task<Hover> Handle(HoverParams request, CancellationToken cancellationToken)
        {
            var hash = Hash.StringHash(request.TextDocument.Uri.GetFileSystemPath());
            var ptr = TreeSitter.Hover(hash, request.Position.Line, request.Position.Character);
            var str = System.Runtime.InteropServices.Marshal.PtrToStringAnsi(ptr);
            var hover = new Hover();
            if (str != null)
            {
                hover.Contents = new MarkedStringsOrMarkupContent(str);
            }
            return Task.FromResult(hover);
        }

        public void SetCapability(HoverCapability capability)
        {
            
        }
    }


}


