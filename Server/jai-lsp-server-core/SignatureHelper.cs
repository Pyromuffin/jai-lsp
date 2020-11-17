using OmniSharp.Extensions.LanguageServer.Protocol.Document;
using OmniSharp.Extensions.LanguageServer.Protocol.Models;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace jai_lsp
{
    class SignatureHelper : SignatureHelpHandler
    {

        HashNamer namer;
        Diagnoser diagnoser;

        public SignatureHelper(HashNamer namer, Diagnoser diagnoser) : base
            (
                new SignatureHelpRegistrationOptions()
                {
                    DocumentSelector = DocumentSelector.ForLanguage("jai"),
                    TriggerCharacters = new Container<string>("(", ","),
                    RetriggerCharacters = new Container<string>(",", ")")
                }
            )
        {
            this.namer = namer;
            this.diagnoser = diagnoser;
        }



        public override Task<SignatureHelp> Handle(SignatureHelpParams request, CancellationToken cancellationToken)
        {
            var currentHash = Hash.StringHash(request.TextDocument.Uri.GetFileSystemPath());

            var pos = request.Position;
            TreeSitter.GetSignature(currentHash, pos.Line, pos.Character, out var signatureArrayPtr, out var parameterCount, out var activeParameter, out var errorCount, out var errorRanges);

            if (signatureArrayPtr.ToInt64() == 0)
                return Task.FromResult(new SignatureHelp());

            var signaturePtr = System.Runtime.InteropServices.Marshal.ReadIntPtr(signatureArrayPtr);
            var signature = System.Runtime.InteropServices.Marshal.PtrToStringAnsi(signaturePtr);


            SignatureInformation info = new SignatureInformation();
            info.Label = signature;


            var paramList = new List<ParameterInformation>();

            if (parameterCount > 0)
            {
                for (int i = 0; i < parameterCount; i++)
                {
                    var paramInfo = new ParameterInformation();
                    var paramPtr = System.Runtime.InteropServices.Marshal.ReadIntPtr(signatureArrayPtr + 8 * (i + 1)); ;
                    paramInfo.Label = System.Runtime.InteropServices.Marshal.PtrToStringAnsi(paramPtr);
                    paramList.Add(paramInfo);
                }
            }

            info.Parameters = new Container<ParameterInformation>(paramList);


            SignatureHelp help = new SignatureHelp();
            help.Signatures = new Container<SignatureInformation>(info);
            help.ActiveParameter = activeParameter;
            help.ActiveSignature = 0;


            if (errorCount > 0)
            {
                List<Diagnostic> diagnostics = new List<Diagnostic>();
                PublishDiagnosticsParams diagnosticsParams = new PublishDiagnosticsParams();
                diagnosticsParams.Uri = request.TextDocument.Uri;
                unsafe
                {
                    for (int i = 0; i < errorCount; i++)
                    {
                        Range* errors = (Range*)errorRanges;
                        var error = errors[i];
                        Diagnostic diagnostic = new Diagnostic();
                        diagnostic.Message = "Extra argument";
                        diagnostic.Range = new OmniSharp.Extensions.LanguageServer.Protocol.Models.Range(
                            new Position(error.startLine, error.startCol),
                            new Position(error.endLine, error.endCol));
                        diagnostics.Add(diagnostic);
                    }
                }

                diagnoser.Add(request.TextDocument.Uri, 1, diagnostics);
            }
            else
            {
                diagnoser.Add(request.TextDocument.Uri, 1, new List<Diagnostic>());
            }

            return Task.FromResult(help);
        }
    }
}
