using System;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using OmniSharp.Extensions.LanguageServer.Protocol.Models;
using OmniSharp.Extensions.LanguageServer.Server;
using Serilog;
using System.IO;
using System.Reflection;
using System.Linq;
using System.Reactive.PlatformServices;
using System.Reactive;

namespace jai_lsp
{
    class Program
    {

        /*
        static async Task Main(string[] args)
        {
            TreeSitter.Init();

            var server = await LanguageServer.From(options =>
                options
                    .WithInput(Console.OpenStandardInput())
                    .WithOutput(Console.OpenStandardOutput())
                    .WithLoggerFactory(new LoggerFactory())
                    .AddDefaultLoggingProvider()
                    .WithMinimumLogLevel(LogLevel.Trace)
                    .WithServices(ConfigureServices)
                    .WithHandler<TextDocumentSyncHandler>()
                    .WithHandler<CompletionHandler>()
                );

            await server.WaitForExit;
        }
        */
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

            Log.Logger = new LoggerConfiguration()
                .Enrich.FromLogContext()
                .WriteTo.File("log.txt", rollingInterval: RollingInterval.Day)
                .MinimumLevel.Verbose()
              .CreateLogger();

            Log.Logger.Information("This only goes file...");

            //IObserver<WorkDoneProgressReport> workDone = null;

            var server = await LanguageServer.From(options =>
                options
                    .WithInput(Console.OpenStandardInput())
                    .WithOutput(Console.OpenStandardOutput())
                    .ConfigureLogging(x => x
                        .AddSerilog()
                        .AddLanguageServer()
                        .SetMinimumLevel(LogLevel.Debug))
                    .WithHandler<TextDocumentHandler>()
                    .WithHandler<CompletionHandler>()
                    //.WithHandler<FoldingRangeHandler>()
                    //.WithHandler<MyWorkspaceSymbolsHandler>()
                    //.WithHandler<MyDocumentSymbolHandler>()
                    .WithHandler<WorkspaceFolderChangeHandler>()
                    .WithHandler<SemanticHighlight>()
                    .WithHandler<GoToDefinitionHandler>()
                    .WithServices(ConfigureServices)
                    .WithServices(x => x.AddLogging(b => b.SetMinimumLevel(LogLevel.Error)))
                    .WithServices(services => {
                        services.AddSingleton(provider =>
                        {
                            var loggerFactory = provider.GetService<ILoggerFactory>();
                            var logger = loggerFactory.CreateLogger<Logjam>();

                            logger.LogInformation("Configuring");

                            return new Logjam(logger);
                        })
                        .AddSingleton<HashNamer>();
                        
                    })
                    /*
                    .OnInitialize(async (server, request, token) => {
                        var manager = server.ProgressManager.WorkDone(request, new WorkDoneProgressBegin()
                        {
                            Title = "Server is starting...",
                            Percentage = 10,
                        });
                        workDone = manager;

                        await Task.Delay(2000);

                        manager.OnNext(new WorkDoneProgressReport()
                        {
                            Percentage = 20,
                            Message = "loading in progress"
                        });
                    })
                    .OnInitialized(async (server, request, response, token) => {
                        workDone.OnNext(new WorkDoneProgressReport()
                        {
                            Percentage = 40,
                            Message = "loading almost done",
                        });

                        await Task.Delay(2000);

                        workDone.OnNext(new WorkDoneProgressReport()
                        {
                            Message = "loading done",
                            Percentage = 100,
                        });
                        workDone.OnCompleted();
                    })
                    */
                     

                    .OnStarted(async (languageServer, result, token) => {

                        /*
                        using var manager = languageServer.ProgressManager.Create(new WorkDoneProgressBegin() { Title = "Doing some work..." });

                        manager.OnNext(new WorkDoneProgressReport() { Message = "doing things..." });
                        await Task.Delay(10000);
                        manager.OnNext(new WorkDoneProgressReport() { Message = "doing things... 1234" });
                        await Task.Delay(10000);
                        manager.OnNext(new WorkDoneProgressReport() { Message = "doing things... 56789" });

                        */
                        using var manager = languageServer.ProgressManager.Create(new WorkDoneProgressBegin() { Title = "Parsing Modules", Percentage = 0, Cancellable = true });
                        var logger = languageServer.Services.GetService<ILogger<Logjam>>();
                        
                        WorkspaceFolderParams wsf = new WorkspaceFolderParams();
                        var wsfresults = await languageServer.Client.SendRequest(wsf, token);
            
                        foreach(var folder in wsfresults)
                        {
                            var path = Path.Combine(folder.Uri.GetFileSystemPath(), "modules");
                            var moduleDirectories = Directory.EnumerateDirectories(path);
                            var count = moduleDirectories.Count();
                            int current = 0;
                            foreach(var moduleDirectory in moduleDirectories)
                            {
                                var moduleFilePath = Path.Combine(moduleDirectory, "module.jai");

                                // chop off path and get module name;
                                var separatorIndex = moduleDirectory.LastIndexOf(Path.DirectorySeparatorChar);
                                var moduleName = moduleDirectory.Substring(separatorIndex + 1);
                                var exists = File.Exists(moduleFilePath);
                                if(exists)
                                {
                                    manager.OnNext(new WorkDoneProgressReport() { Message = moduleFilePath, Percentage = (double)current / count });
                                    TreeSitter.CreateTreeFromPath(moduleFilePath, moduleName);
                                    current++;
                                }
                            }
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
