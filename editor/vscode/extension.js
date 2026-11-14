const vscode = require('vscode');
const { spawn, execFile } = require('child_process');
const path = require('path');
const fs = require('fs');

/** @type {import('child_process').ChildProcess | null} */
let lspProcess = null;
/** @type {vscode.DiagnosticCollection} */
let diagnosticCollection;
/** @type {vscode.StatusBarItem} */
let statusBarItem;
/** @type {vscode.OutputChannel} */
let outputChannel;

let nextRpcId = 1;
/** @type {Map<number, { resolve: Function, reject: Function }>} */
const pendingRequests = new Map();
let rpcBuffer = Buffer.alloc(0);

/**
 * Get configured DMC compiler binary path.
 * @returns {string}
 */
function getCompilerPath() {
    const config = vscode.workspace.getConfiguration('demonic-c');
    return config.get('compilerPath', 'dmc');
}

/**
 * Log message to Demonic C output channel.
 * @param {string} msg
 */
function log(msg) {
    if (outputChannel) {
        outputChannel.appendLine(`[${new Date().toLocaleTimeString()}] ${msg}`);
    }
}

/**
 * Send JSON-RPC message to DMC LSP process.
 * @param {object} payload
 * @returns {Promise<any>}
 */
function sendRpcRequest(method, params) {
    return new Promise((resolve, reject) => {
        if (!lspProcess || !lspProcess.stdin.writable) {
            return reject(new Error('LSP server is not running'));
        }
        const id = nextRpcId++;
        pendingRequests.set(id, { resolve, reject });
        const body = JSON.stringify({ jsonrpc: '2.0', id, method, params });
        const message = `Content-Length: ${Buffer.byteLength(body, 'utf8')}\r\n\r\n${body}`;
        lspProcess.stdin.write(message, 'utf8');
    });
}

/**
 * Send JSON-RPC notification (no response expected).
 * @param {string} method
 * @param {object} params
 */
function sendRpcNotification(method, params) {
    if (!lspProcess || !lspProcess.stdin.writable) return;
    const body = JSON.stringify({ jsonrpc: '2.0', method, params });
    const message = `Content-Length: ${Buffer.byteLength(body, 'utf8')}\r\n\r\n${body}`;
    lspProcess.stdin.write(message, 'utf8');
}

/**
 * Parse incoming JSON-RPC buffer from LSP stdout.
 */
function handleLspData(chunk) {
    rpcBuffer = Buffer.concat([rpcBuffer, chunk]);

    while (true) {
        const headerEnd = rpcBuffer.indexOf('\r\n\r\n');
        if (headerEnd === -1) break;

        const headerStr = rpcBuffer.slice(0, headerEnd).toString('utf8');
        const lengthMatch = headerStr.match(/Content-Length:\s*(\d+)/i);
        if (!lengthMatch) {
            rpcBuffer = rpcBuffer.slice(headerEnd + 4);
            continue;
        }

        const contentLength = parseInt(lengthMatch[1], 10);
        const bodyStart = headerEnd + 4;
        const bodyEnd = bodyStart + contentLength;

        if (rpcBuffer.length < bodyEnd) {
            // Wait for rest of payload
            break;
        }

        const bodyBuf = rpcBuffer.slice(bodyStart, bodyEnd);
        rpcBuffer = rpcBuffer.slice(bodyEnd);

        try {
            const message = JSON.parse(bodyBuf.toString('utf8'));
            handleLspMessage(message);
        } catch (err) {
            log(`Failed to parse LSP payload: ${err.message}`);
        }
    }
}

/**
 * Dispatch LSP message (response or notification).
 * @param {any} msg
 */
function handleLspMessage(msg) {
    if (msg.id !== undefined && pendingRequests.has(msg.id)) {
        const { resolve, reject } = pendingRequests.get(msg.id);
        pendingRequests.delete(msg.id);
        if (msg.error) {
            reject(new Error(msg.error.message || 'LSP error'));
        } else {
            resolve(msg.result);
        }
        return;
    }

    if (msg.method === 'textDocument/publishDiagnostics' && msg.params) {
        const { uri, diagnostics } = msg.params;
        const targetUri = vscode.Uri.parse(uri);
        const vsDiags = (diagnostics || []).map(d => {
            const startLine = (d.range && d.range.start && d.range.start.line) || 0;
            const startChar = (d.range && d.range.start && d.range.start.character) || 0;
            const endLine = (d.range && d.range.end && d.range.end.line) || startLine;
            const endChar = (d.range && d.range.end && d.range.end.character) || (startChar + 1);

            const severity = d.severity === 2
                ? vscode.DiagnosticSeverity.Warning
                : vscode.DiagnosticSeverity.Error;

            const diag = new vscode.Diagnostic(
                new vscode.Range(startLine, startChar, endLine, endChar),
                d.message || 'Diagnostic',
                severity
            );
            diag.source = 'dmc';
            return diag;
        });

        diagnosticCollection.set(targetUri, vsDiags);
    }
}

/**
 * Start or restart the DMC LSP background daemon.
 */
function startLspServer() {
    stopLspServer();

    const config = vscode.workspace.getConfiguration('demonic-c');
    if (!config.get('enableLsp', true)) {
        log('LSP server disabled in configuration');
        return;
    }

    const compiler = getCompilerPath();
    log(`Starting Demonic C Language Server via: ${compiler} lsp`);

    try {
        lspProcess = spawn(compiler, ['lsp'], {
            stdio: ['pipe', 'pipe', 'pipe'],
            env: process.env,
            windowsHide: true
        });

        lspProcess.stdout.on('data', handleLspData);
        lspProcess.stderr.on('data', (d) => {
            log(`[LSP stderr] ${d.toString()}`);
        });

        lspProcess.on('error', (err) => {
            log(`LSP process error: ${err.message}`);
            statusBarItem.text = '$(flame) DMC: Offline';
            statusBarItem.tooltip = `Failed to launch ${compiler}: ${err.message}`;
        });

        lspProcess.on('exit', (code, signal) => {
            log(`LSP process exited with code ${code}, signal ${signal}`);
            lspProcess = null;
            statusBarItem.text = '$(flame) DMC: Stopped';
            statusBarItem.tooltip = 'Demonic C Language Server stopped';
        });

        // Initialize handshake
        sendRpcRequest('initialize', {
            processId: process.pid,
            rootUri: vscode.workspace.workspaceFolders ? vscode.workspace.workspaceFolders[0].uri.toString() : null,
            capabilities: {
                textDocument: {
                    hover: { dynamicRegistration: false },
                    definition: { dynamicRegistration: false },
                    synchronization: { dynamicRegistration: false, willSave: false, didSave: true }
                }
            }
        }).then(() => {
            sendRpcNotification('initialized', {});
            log('LSP handshake initialized successfully');
            statusBarItem.text = '$(flame) DMC 2.0.0';
            statusBarItem.tooltip = 'Demonic C Language Server Active';

            // Sync all open DMC documents
            vscode.workspace.textDocuments.forEach(doc => {
                if (doc.languageId === 'dmc') {
                    sendRpcNotification('textDocument/didOpen', {
                        textDocument: {
                            uri: doc.uri.toString(),
                            languageId: 'dmc',
                            version: doc.version,
                            text: doc.getText()
                        }
                    });
                }
            });
        }).catch(err => {
            log(`Initialize handshake failed: ${err.message}`);
        });

    } catch (e) {
        log(`Failed to spawn compiler LSP: ${e.message}`);
    }
}

/**
 * Terminate active LSP process.
 */
function stopLspServer() {
    if (lspProcess) {
        try {
            sendRpcNotification('exit', {});
            lspProcess.kill();
        } catch (_) {}
        lspProcess = null;
    }
    pendingRequests.clear();
    rpcBuffer = Buffer.alloc(0);
}

/**
 * Fallback CLI diagnostics on file save: runs `dmc check <file>`.
 * @param {vscode.TextDocument} doc
 */
function runCliDiagnostics(doc) {
    if (doc.languageId !== 'dmc' || doc.isUntitled) return;

    const compiler = getCompilerPath();
    execFile(compiler, ['check', doc.fileName], { timeout: 10000 }, (err, stdout, stderr) => {
        const output = (stdout || '') + '\n' + (stderr || '');
        const lines = output.split('\n');
        const diags = [];

        // Match pattern: filename:line:col: (error|warning): message
        const diagRegex = /^(?:.*?):(\d+):(\d+):\s*(error|warning):\s*(.*)$/i;

        for (const line of lines) {
            const m = line.match(diagRegex);
            if (m) {
                const lineNum = Math.max(0, parseInt(m[1], 10) - 1);
                const colNum = Math.max(0, parseInt(m[2], 10) - 1);
                const isWarn = m[3].toLowerCase() === 'warning';
                const msg = m[4].trim();

                const diag = new vscode.Diagnostic(
                    new vscode.Range(lineNum, colNum, lineNum, colNum + 10),
                    msg,
                    isWarn ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Error
                );
                diag.source = 'dmc check';
                diags.push(diag);
            }
        }

        if (diags.length > 0 || !lspProcess) {
            diagnosticCollection.set(doc.uri, diags);
        }
    });
}

/**
 * Get or create the shared Demonic C terminal.
 * @returns {vscode.Terminal}
 */
function getDmcTerminal() {
    const existing = vscode.window.terminals.find(t => t.name === 'Demonic C');
    if (existing) return existing;
    return vscode.window.createTerminal('Demonic C');
}

/**
 * Extension activation entry point.
 * @param {vscode.ExtensionContext} context
 */
function activate(context) {
    outputChannel = vscode.window.createOutputChannel('Demonic C');
    diagnosticCollection = vscode.languages.createDiagnosticCollection('dmc');
    context.subscriptions.push(outputChannel, diagnosticCollection);

    log('Demonic C extension activated');

    // Create status bar item
    statusBarItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 100);
    statusBarItem.text = '$(flame) DMC 2.0.0';
    statusBarItem.tooltip = 'Demonic C Language Support (Click for menu)';
    statusBarItem.command = 'demonic-c.showMenu';
    statusBarItem.show();
    context.subscriptions.push(statusBarItem);

    // Register LSP Document Synchronization
    context.subscriptions.push(
        vscode.workspace.onDidOpenTextDocument(doc => {
            if (doc.languageId === 'dmc') {
                sendRpcNotification('textDocument/didOpen', {
                    textDocument: {
                        uri: doc.uri.toString(),
                        languageId: 'dmc',
                        version: doc.version,
                        text: doc.getText()
                    }
                });
            }
        }),
        vscode.workspace.onDidChangeTextDocument(evt => {
            if (evt.document.languageId === 'dmc') {
                sendRpcNotification('textDocument/didChange', {
                    textDocument: {
                        uri: evt.document.uri.toString(),
                        version: evt.document.version
                    },
                    contentChanges: [{ text: evt.document.getText() }]
                });
            }
        }),
        vscode.workspace.onDidCloseTextDocument(doc => {
            if (doc.languageId === 'dmc') {
                sendRpcNotification('textDocument/didClose', {
                    textDocument: { uri: doc.uri.toString() }
                });
                diagnosticCollection.delete(doc.uri);
            }
        }),
        vscode.workspace.onDidSaveTextDocument(doc => {
            if (doc.languageId === 'dmc') {
                sendRpcNotification('textDocument/didSave', {
                    textDocument: { uri: doc.uri.toString() }
                });

                const config = vscode.workspace.getConfiguration('demonic-c');
                if (config.get('lintOnSave', true)) {
                    runCliDiagnostics(doc);
                }
            }
        })
    );

    // Register Hover Provider via LSP
    context.subscriptions.push(
        vscode.languages.registerHoverProvider('dmc', {
            async provideHover(document, position) {
                if (!lspProcess) return null;
                try {
                    const result = await sendRpcRequest('textDocument/hover', {
                        uri: document.uri.toString(),
                        line: position.line,
                        character: position.character
                    });

                    if (result && result.contents) {
                        const val = typeof result.contents === 'string'
                            ? result.contents
                            : (result.contents.value || '');
                        if (val) {
                            return new vscode.Hover(new vscode.MarkdownString(val));
                        }
                    }
                } catch (e) {
                    // Fail silently for hover
                }
                return null;
            }
        })
    );

    // Register Definition Provider via LSP
    context.subscriptions.push(
        vscode.languages.registerDefinitionProvider('dmc', {
            async provideDefinition(document, position) {
                if (!lspProcess) return null;
                try {
                    const result = await sendRpcRequest('textDocument/definition', {
                        uri: document.uri.toString(),
                        line: position.line,
                        character: position.character
                    });

                    if (result && result.uri && result.range) {
                        const targetUri = vscode.Uri.parse(result.uri);
                        const r = result.range;
                        const targetRange = new vscode.Range(
                            (r.start && r.start.line) || 0,
                            (r.start && r.start.character) || 0,
                            (r.end && r.end.line) || 0,
                            (r.end && r.end.character) || 0
                        );
                        return new vscode.Location(targetUri, targetRange);
                    }
                } catch (e) {
                    // Fail silently
                }
                return null;
            }
        })
    );

    // Register Document Formatter Provider (`dmc fmt`)
    context.subscriptions.push(
        vscode.languages.registerDocumentFormattingEditProvider('dmc', {
            provideDocumentFormattingEdits(document) {
                return new Promise((resolve) => {
                    const compiler = getCompilerPath();
                    execFile(compiler, ['fmt', document.fileName], { timeout: 8000 }, (err) => {
                        if (err) {
                            vscode.window.showWarningMessage(`DMC format failed: ${err.message}`);
                        }
                        // Re-reading disk is handled by VS Code file watcher or reload
                        resolve([]);
                    });
                });
            }
        })
    );

    // --- Commands ---

    // 1. Run Active File
    context.subscriptions.push(
        vscode.commands.registerCommand('demonic-c.run', () => {
            const editor = vscode.window.activeTextEditor;
            if (!editor) {
                vscode.window.showWarningMessage('No active Demonic C file to run.');
                return;
            }
            editor.document.save().then(() => {
                const terminal = getDmcTerminal();
                terminal.show();
                const compiler = getCompilerPath();
                terminal.sendText(`${compiler} run "${editor.document.fileName}"`);
            });
        })
    );

    // 2. Build Active File
    context.subscriptions.push(
        vscode.commands.registerCommand('demonic-c.build', () => {
            const editor = vscode.window.activeTextEditor;
            if (!editor) {
                vscode.window.showWarningMessage('No active Demonic C file to build.');
                return;
            }
            editor.document.save().then(() => {
                const terminal = getDmcTerminal();
                terminal.show();
                const compiler = getCompilerPath();
                terminal.sendText(`${compiler} build "${editor.document.fileName}"`);
            });
        })
    );

    // 3. Run Test Suite
    context.subscriptions.push(
        vscode.commands.registerCommand('demonic-c.test', () => {
            const terminal = getDmcTerminal();
            terminal.show();
            const compiler = getCompilerPath();
            const editor = vscode.window.activeTextEditor;
            if (editor && editor.document.fileName.endsWith('.dmc')) {
                terminal.sendText(`${compiler} test "${editor.document.fileName}"`);
            } else {
                terminal.sendText(`${compiler} test test-files`);
            }
        })
    );

    // 4. Format Document
    context.subscriptions.push(
        vscode.commands.registerCommand('demonic-c.format', () => {
            const editor = vscode.window.activeTextEditor;
            if (!editor) return;
            editor.document.save().then(() => {
                const compiler = getCompilerPath();
                execFile(compiler, ['fmt', editor.document.fileName], (err, stdout) => {
                    if (err) {
                        vscode.window.showErrorMessage(`DMC Format Error: ${err.message}`);
                    } else {
                        vscode.window.showInformationMessage('Demonic C: File formatted.');
                    }
                });
            });
        })
    );

    // 5. Lint Document
    context.subscriptions.push(
        vscode.commands.registerCommand('demonic-c.lint', () => {
            const editor = vscode.window.activeTextEditor;
            if (!editor) return;
            editor.document.save().then(() => {
                runCliDiagnostics(editor.document);
                vscode.window.showInformationMessage('Demonic C: Lint complete.');
            });
        })
    );

    // 6. Interactive Explainer
    context.subscriptions.push(
        vscode.commands.registerCommand('demonic-c.explain', async () => {
            const editor = vscode.window.activeTextEditor;
            let defaultTopic = '';
            if (editor && !editor.selection.isEmpty) {
                defaultTopic = editor.document.getText(editor.selection).trim();
            }

            const topic = await vscode.window.showInputBox({
                prompt: 'Enter Demonic C keyword, type, or concept to explain',
                value: defaultTopic,
                placeHolder: 'e.g. channels, thread, simd, defer, try, arena'
            });

            if (topic) {
                const terminal = getDmcTerminal();
                terminal.show();
                const compiler = getCompilerPath();
                terminal.sendText(`${compiler} explain ${topic}`);
            }
        })
    );

    // 7. Launch REPL
    context.subscriptions.push(
        vscode.commands.registerCommand('demonic-c.repl', () => {
            const terminal = getDmcTerminal();
            terminal.show();
            const compiler = getCompilerPath();
            terminal.sendText(`${compiler} repl`);
        })
    );

    // 8. New Project Wizard
    context.subscriptions.push(
        vscode.commands.registerCommand('demonic-c.newProject', async () => {
            const name = await vscode.window.showInputBox({
                prompt: 'Enter new Demonic C project name',
                placeHolder: 'my-dmc-app'
            });
            if (!name) return;

            const template = await vscode.window.showQuickPick(
                [
                    { label: 'cli', description: 'Standard command-line application (term, io)' },
                    { label: 'polyglot', description: 'Multi-language application (Python, JS, FFI)' },
                    { label: 'game', description: 'Interactive graphical/game project setup' },
                    { label: 'lib', description: 'Reusable shared systems library' }
                ],
                { placeHolder: 'Select project template' }
            );

            const tmpl = template ? template.label : 'cli';
            const compiler = getCompilerPath();

            execFile(compiler, ['new', name, `--template=${tmpl}`], (err) => {
                if (err) {
                    vscode.window.showErrorMessage(`Failed to create project: ${err.message}`);
                    return;
                }
                vscode.window.showInformationMessage(`Created Demonic C project '${name}' (${tmpl})!`, 'Open Folder').then(action => {
                    if (action === 'Open Folder') {
                        const targetPath = path.resolve(process.cwd(), name);
                        vscode.commands.executeCommand('vscode.openFolder', vscode.Uri.file(targetPath), true);
                    }
                });
            });
        })
    );

    // 9. Toolchain Doctor
    context.subscriptions.push(
        vscode.commands.registerCommand('demonic-c.doctor', () => {
            const terminal = getDmcTerminal();
            terminal.show();
            const compiler = getCompilerPath();
            terminal.sendText(`${compiler} heal`);
        })
    );

    // 10. Restart LSP Server
    context.subscriptions.push(
        vscode.commands.registerCommand('demonic-c.restartServer', () => {
            startLspServer();
            vscode.window.showInformationMessage('Demonic C Language Server restarted.');
        })
    );

    // 11. Quick Menu
    context.subscriptions.push(
        vscode.commands.registerCommand('demonic-c.showMenu', async () => {
            const selected = await vscode.window.showQuickPick([
                { label: '$(play) Run Current File', command: 'demonic-c.run' },
                { label: '$(gear) Build Current File', command: 'demonic-c.build' },
                { label: '$(beaker) Run Tests', command: 'demonic-c.test' },
                { label: '$(book) Explain Concept / Keyword', command: 'demonic-c.explain' },
                { label: '$(terminal) Launch REPL', command: 'demonic-c.repl' },
                { label: '$(project) Create New Project', command: 'demonic-c.newProject' },
                { label: '$(check) Lint Active File', command: 'demonic-c.lint' },
                { label: '$(paintcan) Format Active File', command: 'demonic-c.format' },
                { label: '$(pulse) Toolchain Doctor (Heal)', command: 'demonic-c.doctor' },
                { label: '$(sync) Restart Language Server', command: 'demonic-c.restartServer' }
            ], {
                placeHolder: 'Demonic C Development Actions'
            });

            if (selected && selected.command) {
                vscode.commands.executeCommand(selected.command);
            }
        })
    );

    // Start LSP
    startLspServer();
}

/**
 * Extension deactivation.
 */
function deactivate() {
    stopLspServer();
}

module.exports = {
    activate,
    deactivate
};
