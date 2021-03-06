/* --------------------------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the MIT License. See License.txt in the project root for license information.
 * ------------------------------------------------------------------------------------------ */
// tslint:disable
"use strict";

import * as path from "path";

import { workspace, Disposable, ExtensionContext } from "vscode";
import {
    LanguageClient,
    LanguageClientOptions,
    SettingMonitor,
    ServerOptions,
    TransportKind,
    InitializeParams,
    StreamInfo,
    createServerPipeTransport,
} from "vscode-languageclient/node";
import { Trace, createClientPipeTransport } from "vscode-jsonrpc/node";
import { createConnection } from "net";

export function activate(context: ExtensionContext) {

    // The server is implemented in node
    let serverExe = 'dotnet';

    // If the extension is launched in debug mode then the debug server options are used
    // Otherwise the run options are used
    let serverOptions: ServerOptions = {
        run: {
                command: serverExe,
                args: [context.extensionPath +'\\lib\\jai-lsp-server.dll'],
                transport: TransportKind.pipe,
             },
        debug: {
             command: serverExe,
              args: ['C:\\Users\\pyrom\\Documents\\GitHub\\jai-lsp\\Server\\Server\\bin\\Debug\\net5.0\\jai-lsp-server.dll'],
              transport: TransportKind.pipe,
              runtime: "",
            }
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
}

