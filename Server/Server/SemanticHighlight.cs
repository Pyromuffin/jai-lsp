using MediatR;
using Microsoft.Extensions.Logging;
using OmniSharp.Extensions.LanguageServer.Protocol;
using OmniSharp.Extensions.LanguageServer.Protocol.Client.Capabilities;
using OmniSharp.Extensions.LanguageServer.Protocol.Document.Server.Proposals;
using OmniSharp.Extensions.LanguageServer.Protocol.Models;
using OmniSharp.Extensions.LanguageServer.Protocol.Models.Proposals;
using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

#pragma warning disable 618

namespace jai_lsp
{
    public enum TokenType
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

    public enum TokenModifier
    {
        Documentation,
	    Declaration,
	    Definition,
	    Static,
	    Abstract,
	    Deprecated,
	    Readonly,
    }


    class SemanticHighlight : SemanticTokensHandler
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

        ILogger _logger;

        public SemanticHighlight(ILogger<SemanticTokens> logger) : base(new SemanticTokensRegistrationOptions()
        {
            DocumentSelector = DocumentSelector.ForLanguage("jai"),
            Legend = new SemanticTokensLegend() {
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

        protected override Task<SemanticTokensDocument> GetSemanticTokensDocument(ITextDocumentIdentifierParams @params, CancellationToken cancellationToken)
        {
           // var legend = new SemanticTokensLegend();
           // legend.TokenTypes = Enum.GetNames(typeof(TokenType);

            return Task.FromResult(new SemanticTokensDocument(GetRegistrationOptions().Legend));
        }

        protected override Task Tokenize(SemanticTokensBuilder builder, ITextDocumentIdentifierParams identifier, CancellationToken cancellationToken)
        {
            var hash = Hash.StringHash(identifier.TextDocument.Uri.GetFileSystemPath());

            var now = DateTime.Now;
            IntPtr tokensPtr = IntPtr.Zero;
            int count = 0;
            long internalMicros = TreeSitter.GetTokens(hash, out tokensPtr, out count); // this is syncrhonous because we just get the data from native side.
            var then = DateTime.Now;
            var elapsed = then - now;
            _logger.LogInformation("Elapsed time for C++ tokens: " + elapsed.TotalMilliseconds + " native time: " + internalMicros);

            unsafe
            {
                SemanticToken* ptr = (SemanticToken*)tokensPtr;
                for (int i = 0; i < count; i++)
                {
                    builder.Push(ptr[i].line, ptr[i].col, ptr[i].length, (int)ptr[i].type, (int)ptr[i].modifier);
                }
            }

            return Unit.Task;
        }
    }
}
