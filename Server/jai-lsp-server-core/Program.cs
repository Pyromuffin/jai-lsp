using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using OmniSharp.Extensions.LanguageServer.Protocol.Models;
using OmniSharp.Extensions.LanguageServer.Server;
using Serilog;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;

namespace jai_lsp
{
    class Program
    {


        static void Main(string[] args)
        {
            MainAsync(args).Wait();
        }

        static async Task MainAsync(string[] args)
        {

#if DEBUG 
            System.Diagnostics.Debugger.Launch();
            while (!System.Diagnostics.Debugger.IsAttached)
            {
                await Task.Delay(100);
            }
#endif
            TreeSitter.Init();

            var server = await LanguageServer.From(options =>
                options
                    .WithInput(Console.OpenStandardInput())
                    .WithOutput(Console.OpenStandardOutput())
                    .ConfigureLogging(x => x
                        .AddSerilog()
                        .AddLanguageProtocolLogging()
#if DEBUG
                        .SetMinimumLevel(LogLevel.Error))
#else
                        .SetMinimumLevel(LogLevel.Error))
#endif
                    .WithHandler<SignatureHelper>()
                    .WithHandler<Definer>()
                    .WithHandler<Hoverer>()
                    .WithHandler<TextDocumentHandler>()
                    .WithHandler<CompletionHandler>()
                    //  .WithHandler<WorkspaceFolderChangeHandler>()

                    // handlers added after here dont work i think!
                    .WithHandler<SemanticTokensHandler>()

                    .WithServices(ConfigureServices)
                    .WithServices(x => x.AddLogging(b => b.SetMinimumLevel(LogLevel.Error)))
                    .WithServices(services =>
                    {
                        services.AddSingleton(provider =>
                        {
                            var loggerFactory = provider.GetService<ILoggerFactory>();
                            var logger = loggerFactory.CreateLogger<Logjam>();

                            logger.LogInformation("Configuring");

                            return new Logjam(logger);
                        })
                        .AddSingleton<HashNamer>()
                        .AddSingleton<Diagnoser>();
                    })


                    .OnStarted(async (languageServer, token) =>
                    {

                        using var manager = await languageServer.WorkDoneManager.Create(new WorkDoneProgressBegin() { Title = "Parsing Modules", Percentage = 0, Cancellable = true });

                        var logger = languageServer.Services.GetService<ILogger<Logjam>>();
                        var namer = languageServer.Services.GetService<HashNamer>();
                        var diagnoser = languageServer.Services.GetService<Diagnoser>();
                        diagnoser.server = languageServer;

                        WorkspaceFolderParams wsf = new WorkspaceFolderParams();
                        var wsfresults = await languageServer.Client.SendRequest(wsf, token);

                        foreach (var folder in wsfresults)
                        {
                            // find all the jai files and hash their paths
                            string[] files = Directory.GetFiles(folder.Uri.GetFileSystemPath(), "*.jai", SearchOption.AllDirectories);
                            foreach (var f in files)
                            {
                                namer.hashToName[Hash.StringHash(f)] = f;
                            }

                            var path = Path.Combine(folder.Uri.GetFileSystemPath(), "modules");
                            var moduleDirectories = Directory.EnumerateDirectories(path);
                            var count = moduleDirectories.Count();
                            int current = 0;
                            List<Task<string>> tasks = new List<Task<string>>();
                            long totalTime = 0;

                            foreach (var moduleDirectory in moduleDirectories)
                            {
                                var moduleFilePath = Path.Combine(moduleDirectory, "module.jai");

                                // chop off path and get module name;
                                var separatorIndex = moduleDirectory.LastIndexOf(Path.DirectorySeparatorChar);
                                var moduleName = moduleDirectory.Substring(separatorIndex + 1);
                                var exists = File.Exists(moduleFilePath);

                                if (exists)
                                {
                                    var task = Task.Run(

                                            () =>
                                            {
                                                TreeSitter.CreateTreeFromPath(moduleFilePath, moduleName);
                                                return moduleFilePath;
                                            }

                                         );
                                    tasks.Add(task);
                                    //totalTime += TreeSitter.CreateTreeFromPath(moduleFilePath, moduleName);
                                    //current++;
                                }

                            }


                            while (current < tasks.Count)
                            {
                                var task = await Task.WhenAny(tasks);
                                current++;
                                manager.OnNext(new WorkDoneProgressReport() { Message = task.Result, Percentage = (double)current / count });
                            }

                            logger.LogInformation("Total time for serial module parsing: " + totalTime + " micros");

                        }
                        manager.OnCompleted();
                    })

            );

            // lmao i have no idea

            await server.WaitForExit;
        }



        static void ConfigureServices(IServiceCollection services)
        {
        }
    }

    internal class Logjam
    {
        private readonly ILogger<Logjam> _logger;

        public Logjam(ILogger<Logjam> logger)
        {
            logger.LogInformation("inside logjam ctor");
            _logger = logger;
        }
    }
}
