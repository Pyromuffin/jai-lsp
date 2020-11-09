using OmniSharp.Extensions.LanguageServer.Protocol.Models;
using OmniSharp.Extensions.LanguageServer.Protocol.Client.Capabilities;
using OmniSharp.Extensions.LanguageServer.Protocol.Document;
using System.Threading;
using System.Threading.Tasks;
using System.Collections.Generic;
using MediatR.Pipeline;
using System.Linq;
using System;
using OmniSharp.Extensions.LanguageServer.Protocol;
using OmniSharp.Extensions.LanguageServer.Protocol.Workspace;

namespace jai_lsp
{
    class SignatureHelper : SignatureHelpHandler
    {

        HashNamer namer;


        public SignatureHelper(HashNamer namer) : base
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
        }


        int triggerStartPosition = -1;
        int triggerStartLine = -1;
        ulong currentHash;



        public override Task<SignatureHelp> Handle(SignatureHelpParams request, CancellationToken cancellationToken)
        {

            /*
            if (request.Context.IsRetrigger && request.Context.TriggerKind == SignatureHelpTriggerKind.TriggerCharacter && request.Context.TriggerCharacter == ",")
            {
                var signatureHelp = request.Context.ActiveSignatureHelp;
                signatureHelp.ActiveParameter++;
                return Task.FromResult(signatureHelp);
            }


            if (request.Context.IsRetrigger && request.Context.TriggerKind == SignatureHelpTriggerKind.ContentChange)
            {
                var triggerPos = request.Position.Character;
                var triggerLine = request.Position.Line;

                if (triggerPos < triggerStartPosition || triggerLine != triggerStartLine)
                {
                    return Task.FromResult(new SignatureHelp());
                }

                var linePtr = TreeSitter.GetLine(currentHash, triggerLine);
                var line = System.Runtime.InteropServices.Marshal.PtrToStringAnsi(linePtr);

                int commas = 0;
                for (int i = 0; i < line.Length; i++)
                {
                    if (i == triggerPos)
                        break;

                    if (line[i] == ',')
                    {
                        commas++;
                    }
                }

                request.Context.ActiveSignatureHelp.ActiveParameter = commas;
                return Task.FromResult(request.Context.ActiveSignatureHelp);
            }


            if (request.Context.TriggerCharacter == ")")
            {
                return Task.FromResult(new SignatureHelp());
            }

            triggerStartPosition = request.Position.Character;
            triggerStartLine = request.Position.Line;

            /* leaving this here for posterity.
             * 
            // also! add a close parenthesis!
            var edit = new WorkspaceEdit();
            edit.Changes = new Dictionary<DocumentUri, IEnumerable<TextEdit>>();
            var uri = request.TextDocument.Uri;
            TextEdit textEdit = new TextEdit();
            textEdit.NewText = ")";
            var insertPos = request.Position;
            insertPos.Character++;
            textEdit.Range = new OmniSharp.Extensions.LanguageServer.Protocol.Models.Range(insertPos, insertPos);
            var array = new TextEdit[] { textEdit };

            edit.Changes.Add(uri, array);
            ApplyWorkspaceEditParams parameters = new ApplyWorkspaceEditParams();
            parameters.Edit = edit;
            namer.server.ApplyWorkspaceEdit(parameters);
            */

            currentHash = Hash.StringHash(request.TextDocument.Uri.GetFileSystemPath());

            var pos = request.Position;
            TreeSitter.GetSignature(currentHash, pos.Line, pos.Character, out var signatureArrayPtr, out var parameterCount, out var activeParameter);

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

     

            return Task.FromResult(help);
        }
    }
}
