using System;
using System.Threading.Tasks;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Newtonsoft.Json.Linq;
using OmniSharp.Extensions.LanguageServer.Protocol.Models;
using OmniSharp.Extensions.LanguageServer.Server;
using Serilog;

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
            // Debugger.Launch();
            // while (!System.Diagnostics.Debugger.IsAttached)
            // {
            //     await Task.Delay(100);
            // }

            TreeSitter.Init();

            Log.Logger = new LoggerConfiguration()
                .Enrich.FromLogContext()
                .WriteTo.File("log.txt", rollingInterval: RollingInterval.Day)
                .MinimumLevel.Verbose()
              .CreateLogger();

            Log.Logger.Information("This only goes file...");

            IObserver<WorkDoneProgressReport> workDone = null;

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
                    .WithHandler<SemanticHighlight>()
                    .WithServices(ConfigureServices)
                    .WithServices(x => x.AddLogging(b => b.SetMinimumLevel(LogLevel.Trace)))
                    .WithServices(services => {
                        services.AddSingleton(provider => {
                            var loggerFactory = provider.GetService<ILoggerFactory>();
                            var logger = loggerFactory.CreateLogger<Logjam>();

                            logger.LogInformation("Configuring");

                            return new Logjam(logger);
                        });
                        services.AddSingleton(new ConfigurationItem()
                        {
                            Section = "typescript",
                        }).AddSingleton(new ConfigurationItem()
                        {
                            Section = "terminal",
                        });
                    })
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
                    .OnStarted(async (languageServer, result, token) => {
                        using var manager = languageServer.ProgressManager.Create(new WorkDoneProgressBegin() { Title = "Doing some work..." });

                        manager.OnNext(new WorkDoneProgressReport() { Message = "doing things..." });
                        await Task.Delay(10000);
                        manager.OnNext(new WorkDoneProgressReport() { Message = "doing things... 1234" });
                        await Task.Delay(10000);
                        manager.OnNext(new WorkDoneProgressReport() { Message = "doing things... 56789" });
                        
                        var logger = languageServer.Services.GetService<ILogger<Logjam>>();

                        var configuration = await languageServer.Configuration.GetConfiguration(
                            new ConfigurationItem()
                            {
                                Section = "typescript",
                            }, new ConfigurationItem()
                            {
                                Section = "terminal",
                            });

                        var baseConfig = new JObject();
                        foreach (var config in languageServer.Configuration.AsEnumerable())
                        {
                            baseConfig.Add(config.Key, config.Value);
                        }

                        logger.LogInformation("Base Config: {Config}", baseConfig);

                        var scopedConfig = new JObject();
                        foreach (var config in configuration.AsEnumerable())
                        {
                            scopedConfig.Add(config.Key, config.Value);
                        }

                        logger.LogInformation("Scoped Config: {Config}", scopedConfig);

                    })
            );

            await server.WaitForExit;
        }



        static void ConfigureServices(IServiceCollection services)
        {
            services.AddSingleton<BufferManager>();
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
