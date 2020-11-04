using OmniSharp.Extensions.LanguageServer.Protocol.Models;
using OmniSharp.Extensions.LanguageServer.Protocol.Client.Capabilities;
using OmniSharp.Extensions.LanguageServer.Protocol.Document;
using System.Threading;
using System.Threading.Tasks;

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
                    TriggerCharacters = new Container<string>("("),
                    RetriggerCharacters = new Container<string>(",", ")")
                }
            )
        {
            this.namer = namer;
        }



        public override Task<SignatureHelp> Handle(SignatureHelpParams request, CancellationToken cancellationToken)
        {
            return Task.FromResult(new SignatureHelp());

            SignatureInformation info = new SignatureInformation();
            info.Label = "Taste(using juice : Juice)";

            ParameterInformation param1 = new ParameterInformation();
            param1.Label = "nice : int";
            ParameterInformation param2 = new ParameterInformation();
            param2.Label = "cool : string";

            info.Parameters = new Container<ParameterInformation>(param1, param2);

            SignatureHelp help = new SignatureHelp();
            help.Signatures = new Container<SignatureInformation>(info);
            help.ActiveParameter = 0;
            help.ActiveSignature = 0;

            if (request.Context.TriggerCharacter == ",")
            {
                help.ActiveParameter = 1;
            }

            if (request.Context.TriggerCharacter == ")")
            {
                return Task.FromResult(new SignatureHelp());
            }


            return Task.FromResult(help);
        }
    }
}
