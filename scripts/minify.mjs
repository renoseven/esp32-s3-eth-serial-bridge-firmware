#!/usr/bin/env node
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// Minify .js, .css, .html, and .htm files.
//
//   minify.mjs <input> [-o output] [--no-mangle]
//
// Chooses the minifier from the input extension. JS is bundled as IIFE via
// esbuild; --no-mangle keeps JS identifiers unchanged. Without -o, writes stdout.

import { Buffer } from 'node:buffer';
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, extname, resolve } from 'node:path';
import { argv, exit, stderr, stdout } from 'node:process';

import minifyHtml from '@minify-html/node';
import * as esbuild from 'esbuild';

const USAGE =
    'usage: minify.mjs <input> [-o output] [--no-mangle]\n' +
    '  .js .css .html .htm supported; --no-mangle applies to JS only\n';

const MINIFIERS = {
    '.js': 'js',
    '.css': 'css',
    '.html': 'html',
    '.htm': 'html'
};

function parseArgs(args) {
    let output = null;
    let mangle = true;
    const inputs = [];

    for (let i = 0; i < args.length; i++) {
        const arg = args[i];
        if (arg === '--no-mangle') {
            mangle = false;
        } else if (arg === '-o' || arg === '--output') {
            const next = args[++i];
            if (!next) {
                stderr.write(`${USAGE}error: ${arg} requires a path\n`);
                exit(1);
            }
            output = next;
        } else if (arg.startsWith('-')) {
            stderr.write(`${USAGE}error: unknown option: ${arg}\n`);
            exit(1);
        } else {
            inputs.push(arg);
        }
    }

    if (inputs.length !== 1) {
        stderr.write(USAGE);
        exit(1);
    }

    return { input: inputs[0], output, mangle };
}

function minifierFor(path) {
    const kind = MINIFIERS[extname(path).toLowerCase()];
    if (!kind) {
        stderr.write(`${USAGE}error: unsupported extension: ${path}\n`);
        exit(1);
    }
    return kind;
}

const { input, output, mangle } = parseArgs(argv.slice(2));
const inputPath = resolve(input);
const kind = minifierFor(inputPath);
const source = readFileSync(inputPath, 'utf8');
const resolveDir = dirname(inputPath);

const minifiers = {
    async js(text) {
        // Bundle as IIFE so top-level declarations can be mangled (transform only renames locals).
        const { outputFiles } = await esbuild.build({
            stdin: { contents: text, loader: 'js', resolveDir },
            bundle: true,
            format: 'iife',
            target: 'es2022',
            write: false,
            minifyWhitespace: true,
            minifySyntax: true,
            minifyIdentifiers: mangle
        });
        return outputFiles[0].text;
    },
    async css(text) {
        const { code } = await esbuild.transform(text, { loader: 'css', minify: true });
        return code;
    },
    html(text) {
        const minified = minifyHtml.minify(Buffer.from(text), { minify_js: false, minify_css: false }).toString('utf8');
        return restoreSubmitButtonTypes(minified);
    }
};

/** @minify-html/node drops default type=submit; script.js selects save via [type="submit"]. */
function restoreSubmitButtonTypes(html) {
    return html.replace(
        /<button(?=[^>]*\bdata-action=(?:save-[^\s>"']+|"(?:save-[^"]+)"|'(?:save-[^']+)'))(?![^>]*\btype=)/gi,
        '<button type="submit"'
    );
}

const result = await minifiers[kind](source);
if (output) {
    writeFileSync(resolve(output), result);
} else {
    stdout.write(result);
}
