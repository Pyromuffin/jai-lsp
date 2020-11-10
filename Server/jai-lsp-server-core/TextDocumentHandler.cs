using System;
using System.Collections.Concurrent;
using System.Threading;
using System.Threading.Tasks;
using MediatR;
using Microsoft.Extensions.Logging;
using OmniSharp.Extensions.LanguageServer.Protocol;
using OmniSharp.Extensions.LanguageServer.Protocol.Client.Capabilities;
using OmniSharp.Extensions.LanguageServer.Protocol.Document;
using OmniSharp.Extensions.LanguageServer.Protocol.Models;
using OmniSharp.Extensions.LanguageServer.Protocol.Progress;
using OmniSharp.Extensions.LanguageServer.Protocol.Server;
using OmniSharp.Extensions.LanguageServer.Protocol.Server.Capabilities;
using OmniSharp.Extensions.LanguageServer.Protocol.Server.WorkDone;
using OmniSharp.Extensions.LanguageServer.Protocol.Workspace;

namespace jai_lsp
{
    public class HashNamer
    {
        public ConcurrentDictionary<ulong, string> hashToName = new ConcurrentDictionary<ulong, string>();
    }


    class TextDocumentHandler : ITextDocumentSyncHandler
    {

        HashNamer hashNamer;
        private readonly ILogger<TextDocumentHandler> _logger;
        private readonly ILanguageServerConfiguration _configuration;

        private readonly DocumentSelector _documentSelector = new DocumentSelector(
            new DocumentFilter()
            {
                Pattern = "**/*.jai"
            }
        );

        private SynchronizationCapability _capability;

        public TextDocumentHandler(ILogger<TextDocumentHandler> logger, Logjam foo,
            ILanguageServerConfiguration configuration,
            HashNamer hashNamer
            )
        {
            _logger = logger;
            _configuration = configuration;
            this.hashNamer = hashNamer;
        }

        public TextDocumentSyncKind Change { get; } = TextDocumentSyncKind.Incremental;

        public Task<Unit> Handle(DidOpenTextDocumentParams notification, CancellationToken token)
        {
            var path = notification.TextDocument.Uri.GetFileSystemPath();
            var hash = Hash.StringHash(path);
            hashNamer.hashToName[hash] = path;

            TreeSitter.CreateTree(path, notification.TextDocument.Text, notification.TextDocument.Text.Length);
            return Unit.Task;
        }

        public Task<Unit> Handle(DidChangeTextDocumentParams request, CancellationToken token)
        {
            var documentPath = request.TextDocument.Uri.GetFileSystemPath();
            var hash = Hash.StringHash(documentPath);
            foreach (var change in request.ContentChanges)
            {
                var range = change.Range;
                var start = range.Start;
                var end = range.End;

                TreeSitter.EditTree(hash, change.Text, start.Line, start.Character, end.Line, end.Character, change.Text.Length, change.RangeLength);
            }

            TreeSitter.UpdateTree(hash);

            return Unit.Task;
        }

        TextDocumentChangeRegistrationOptions IRegistration<TextDocumentChangeRegistrationOptions>.
            GetRegistrationOptions()
        {
            return new TextDocumentChangeRegistrationOptions()
            {
                DocumentSelector = _documentSelector,
                SyncKind = Change
            };
        }

        public void SetCapability(SynchronizationCapability capability)
        {
            _capability = capability;
        }

        TextDocumentRegistrationOptions IRegistration<TextDocumentRegistrationOptions>.GetRegistrationOptions()
        {
            return new TextDocumentRegistrationOptions()
            {
                DocumentSelector = _documentSelector,
            };
        }

        public Task<Unit> Handle(DidCloseTextDocumentParams notification, CancellationToken token)
        {
            if (_configuration.TryGetScopedConfiguration(notification.TextDocument.Uri, out var disposable))
            {
                disposable.Dispose();
            }

            return Unit.Task;
        }

        public Task<Unit> Handle(DidSaveTextDocumentParams notification, CancellationToken token)
        {
            return Unit.Task;
        }

        TextDocumentSaveRegistrationOptions IRegistration<TextDocumentSaveRegistrationOptions>.GetRegistrationOptions()
        {
            return new TextDocumentSaveRegistrationOptions()
            {
                DocumentSelector = _documentSelector,
                IncludeText = true
            };
        }

        public TextDocumentAttributes GetTextDocumentAttributes(DocumentUri uri)
        {
            return new TextDocumentAttributes(uri, "jai");
        }
    }
}
