/* --------------------------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the MIT License. See License.txt in the project root for license information.
 * ------------------------------------------------------------------------------------------ */
// tslint:disable
'use strict';

import * as path from "path";

import { workspace, Disposable, ExtensionContext } from "vscode";
import {
    LanguageClient,
    LanguageClientOptions,
    SettingMonitor,
    ServerOptions,
    TransportKind,
    InitializeParams
} from "vscode-languageclient";
import { Trace } from "vscode-jsonrpc";

export function activate(context: ExtensionContext) {

    // The server is implemented in node
    let serverExe = 'dotnet';

    // If the extension is launched in debug mode then the debug server options are used
    // Otherwise the run options are used
    let serverOptions: ServerOptions = {
        run: { command: serverExe, args: ['C:\\Users\\pyrom\\Documents\\GitHub\\jai-lsp\\Server\\Server\\bin\\Release\\net5.0\\jai-lsp-server.dll'] },
        debug: { command: serverExe, args: ['C:\\Users\\pyrom\\Documents\\GitHub\\jai-lsp\\Server\\x64\\Debug\\ParserTester.exe'] }
    }

    // Options to control the language client
    let clientOptions: LanguageClientOptions = {
        // Register the server for plain text documents
        documentSelector: [
            {
                pattern: "**/*.jai"
            },
        ],
        progressOnInitialization: true,
        synchronize: {
            // Synchronize the setting section 'languageServerExample' to the server
            configurationSection: "jai_lsp",
            fileEvents: workspace.createFileSystemWatcher("**/*.jai")
        }
    };

    // Create the language client and start the client.
    const client = new LanguageClient(
        "jai_lsp",
        "jai language server",
        serverOptions,
        clientOptions
    );
    client.registerProposedFeatures();
    client.trace = Trace.Verbose;
    let disposable = client.start();

    // Push the disposable to the context's subscriptions so that the
    // client can be deactivated on extension deactivation
    context.subscriptions.push(disposable);


    /*
    const tokenTypes = ['class', 'interface', 'enum', 'function', 'variable'];
    const tokenModifiers = ['declaration', 'documentation'];
    const legend = new vscode.SemanticTokensLegend(tokenTypes, tokenModifiers);
    
    const provider: vscode.DocumentSemanticTokensProvider = {
      provideDocumentSemanticTokens( document: vscode.TextDocument ):
       vscode.ProviderResult<vscode.SemanticTokens> {
          let builder = new vscode.SemanticTokensBuilder(legend);
          builder.push(0,0,10, 1,0);

        return builder.build();
      }
    };
    
    const selector = { language: 'jai', scheme: 'file' };
    
    vscode.languages.registerDocumentSemanticTokensProvider(selector, provider, legend);
    */
}

