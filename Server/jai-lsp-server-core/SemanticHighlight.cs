using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Logging;
using OmniSharp.Extensions.LanguageServer.Protocol;
using OmniSharp.Extensions.LanguageServer.Protocol.Client.Capabilities;
using OmniSharp.Extensions.LanguageServer.Protocol.Document;
using OmniSharp.Extensions.LanguageServer.Protocol.Document.Proposals;
using OmniSharp.Extensions.LanguageServer.Protocol.Models;
using OmniSharp.Extensions.LanguageServer.Protocol.Models.Proposals;

#pragma warning disable 618

namespace jai_lsp
{

        public enum TokenType : byte
        {
            Documentation,
            Comment,
            Keyword,
            String,
            Number,
            Regexp,
            Operator,
            Namespace,
            Type,
            Struct,
            Class,
            Interface,
            Enum,
            TypeParameter,
            Function,
            Member,
            Property,
            Macro,
            Variable,
            Parameter,
            Label,
            EnumMember,
        }

        public enum TokenModifier : byte
        {
            Documentation,
            Declaration,
            Definition,
            Static,
            Abstract,
            Deprecated,
            Readonly,
        }


#pragma warning disable 618
        public class SemanticTokensHandler : SemanticTokensHandlerBase
    {

        static string[] typeNames =
{
        "documentation",
        "comment",
        "keyword",
        "string",
        "number",
        "regexp",
        "operator",
        "namespace",
        "type",
        "struct",
        "class",
        "interface",
        "enum",
        "typeParameter",
        "function",
        "member",
        "property",
        "macro",
        "variable",
        "parameter",
        "label",
        "enumMember"
        };

        static string[] modifierNames =
        {
           "documentation",
           "declaration",
           "definition",
           "static",
           "abstract",
           "deprecated",
           "readonly",
        };




        private readonly ILogger _logger;
        HashNamer namer;
        Diagnoser diagnoser;

        public SemanticTokensHandler(ILogger<SemanticTokensHandler> logger, HashNamer namer, Diagnoser diagnoser) : base(
            new SemanticTokensRegistrationOptions
            {
                DocumentSelector = DocumentSelector.ForLanguage("jai"),
                Legend = new SemanticTokensLegend()
                {
                    TokenTypes = typeNames,
                    TokenModifiers = modifierNames,
                },
                Full = new SemanticTokensCapabilityRequestFull
                {
                    Delta = true
                },
                Range = true
            }
        )
        {
            _logger = logger;
            this.namer = namer;
            this.diagnoser = diagnoser;
        }

        public override async Task<SemanticTokens> Handle(
            SemanticTokensParams request, CancellationToken cancellationToken
        )
        {
            var result = await base.Handle(request, cancellationToken);
            return result;
        }

        public override async Task<SemanticTokens> Handle(
            SemanticTokensRangeParams request, CancellationToken cancellationToken
        )
        {
            var result = await base.Handle(request, cancellationToken);
            return result;
        }

        public override async Task<SemanticTokensFullOrDelta?> Handle(
            SemanticTokensDeltaParams request,
            CancellationToken cancellationToken
        )
        {
            var result = await base.Handle(request, cancellationToken);
            return result;
        }

        protected override async Task Tokenize(
            SemanticTokensBuilder builder, ITextDocumentIdentifierParams identifier,
            CancellationToken cancellationToken
        )
        {
            var hash = Hash.StringHash(identifier.TextDocument.Uri.GetFileSystemPath());

            var now = DateTime.Now;
            IntPtr tokensPtr = IntPtr.Zero;
            int count = 0;
            long internalMicros = TreeSitter.GetTokens
                (hash, out tokensPtr, out count); // this is syncrhonous because we just get the data from native side.
            var then = DateTime.Now;
            var elapsed = then - now;
            _logger.LogInformation("Elapsed time for C++ tokens: " + elapsed.TotalMilliseconds + " native time: " + internalMicros);

            List<Diagnostic> diagnostics = new List<Diagnostic>();

            unsafe
            {
                SemanticToken* ptr = (SemanticToken*)tokensPtr;
                for (int i = 0; i < count; i++)
                {
                    if ((int)ptr[i].type == 255)
                    {
                        Diagnostic diag = new Diagnostic();
                        diag.Severity = DiagnosticSeverity.Error;
                        diag.Range = new OmniSharp.Extensions.LanguageServer.Protocol.Models.Range();
                        diag.Range.Start = new Position(ptr[i].line, ptr[i].col);
                        diag.Range.End = new Position(ptr[i].line, ptr[i].col + ptr[i].length);
                        diag.Message = "undeclared identifer";
                        diagnostics.Add(diag);

                        continue;
                    }

                    builder.Push(ptr[i].line, ptr[i].col, ptr[i].length, (int)ptr[i].type, (int)ptr[i].modifier);
                }
            }

            diagnoser.Add(identifier.TextDocument.Uri, 0, diagnostics);
            diagnoser.Publish(identifier.TextDocument.Uri);
        }

        protected override Task<SemanticTokensDocument>
            GetSemanticTokensDocument(ITextDocumentIdentifierParams @params, CancellationToken cancellationToken) =>
            Task.FromResult(new SemanticTokensDocument(GetRegistrationOptions().Legend));


    }
#pragma warning restore 618
}
