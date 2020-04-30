using OmniSharp.Extensions.LanguageServer.Protocol.Client.Capabilities;
using OmniSharp.Extensions.LanguageServer.Protocol.Models;
using OmniSharp.Extensions.LanguageServer.Protocol.Server;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace jai_lsp
{
    internal class CompletionHandler : ICompletionHandler
    {
        private const string PackageReferenceElement = "PackageReference";
        private const string IncludeAttribute = "Include";
        private const string VersionAttribute = "Version";
        private static readonly char[] EndElement = new[] { '>' };

        private readonly ILanguageServer _router;
        private readonly BufferManager _bufferManager;

        private readonly DocumentSelector _documentSelector = new DocumentSelector(
            new DocumentFilter()
            {
                Pattern = "**/*.jai"
            }
        );

        private CompletionCapability _capability;

        public CompletionHandler(ILanguageServer router, BufferManager bufferManager)
        {
            _router = router;
            _bufferManager = bufferManager;
        }

        public CompletionRegistrationOptions GetRegistrationOptions()
        {
            return new CompletionRegistrationOptions
            {
                DocumentSelector = _documentSelector,
                ResolveProvider = false
            };
        }

        public async Task<CompletionList> Handle(CompletionParams request, CancellationToken cancellationToken)
        {
            //_router.Window.LogInfo("Received Completion Request!");
       
            var documentPath = request.TextDocument.Uri.ToString();
            var text = _bufferManager.GetBuffer(documentPath);

            if (text == null)
            {
                return new CompletionList();
            }

            var names = TreeSitter.CoolParse(text);
            var nameList = names.Split(",");
            List<CompletionItem> items = new List<CompletionItem>(nameList.Length);
            
            foreach(var name in nameList )
            {
                var completion = new CompletionItem();
                completion.Label = name;
                items.Add(completion);
            }

            return new CompletionList(items);
            /*
            var syntaxTree = Parser.Parse(buffer);

            var position = GetPosition(buffer.GetText(0, buffer.Length),
                (int)request.Position.Line,
                (int)request.Position.Character);

            var node = syntaxTree.FindNode(position);

            var attribute = node.AncestorNodes().OfType<XmlAttributeSyntax>().FirstOrDefault();
            if (attribute != null && node.ParentElement.Name.Equals(PackageReferenceElement))
            {
                if (attribute.Name.Equals(IncludeAttribute))
                {
                    var completions = await _nuGetService.GetPackages(attribute.Value);

                    var diff = position - attribute.ValueNode.Start;

                    return new CompletionList(completions.Select(x => new CompletionItem
                    {
                        Label = x,
                        Kind = CompletionItemKind.Reference,
                        TextEdit = new TextEdit
                        {
                            NewText = x,
                            Range = new Range(
                                new Position
                                {
                                    Line = request.Position.Line,
                                    Character = request.Position.Character - diff + 1
                                }, new Position
                                {
                                    Line = request.Position.Line,
                                    Character = request.Position.Character - diff + attribute.ValueNode.Width - 1
                                })
                        }
                    }), isIncomplete: completions.Count > 1);
                }
                else if (attribute.Name.Equals(VersionAttribute))
                {
                    var includeNode = node.ParentElement.Attributes.FirstOrDefault(x => x.Name.Equals(IncludeAttribute));

                    if (includeNode != null && !string.IsNullOrEmpty(includeNode.Value))
                    {
                        var versions = await _nuGetService.GetPackageVersions(includeNode.Value, attribute.Value);

                        var diff = position - attribute.ValueNode.Start;

                        return new CompletionList(versions.Select(x => new CompletionItem
                        {
                            Label = x,
                            Kind = CompletionItemKind.Reference,
                            TextEdit = new TextEdit
                            {
                                NewText = x,
                                Range = new Range(
                                    new Position
                                    {
                                        Line = request.Position.Line,
                                        Character = request.Position.Character - diff + 1
                                    }, new Position
                                    {
                                        Line = request.Position.Line,
                                        Character = request.Position.Character - diff + attribute.ValueNode.Width - 1
                                    })
                            }
                        }));
                    }
                }
            }

            return new CompletionList();
            */
        }

        private static int GetPosition(string buffer, int line, int col)
        {
            var position = 0;
            for (var i = 0; i < line; i++)
            {
                position = buffer.IndexOf('\n', position) + 1;
            }
            return position + col;
        }

        public void SetCapability(CompletionCapability capability)
        {
            _capability = capability;
        }
    }
}
