// SPDX-License-Identifier: MS-PL
//
// plans/plan_cabi.md CABI-14: load the built module and call into it.
//
// Two things are checked here. That the module instantiates and a call reaches compiled C and comes
// back with the right answer -- the first thing cna-ts needs to trust. And, since CABI-29, that the
// module exports every route the current headers declare.
//
// The second check exists because its absence was a real defect: the export list is generated from
// the headers, but the build rule that generated it did not depend on them, so declaring a route
// left the built module silently short of it. -sEXPORTED_FUNCTIONS produces no diagnostic for a
// name that is merely absent, so nothing failed -- the routes were simply not there. The build-rule
// dependency is the fix; this is the gate that says so if it ever regresses.

import { argv, exit } from 'node:process';
import { readFileSync } from 'node:fs';

const modulePath = argv[2];
const exportsPath = argv[3];
if (!modulePath) {
    console.error('usage: module_smoke.mjs <path to cna_c_api.mjs> [path to exports json]');
    exit(2);
}

const createCnaCApi = (await import(modulePath)).default;
const cna = await createCnaCApi();

// The one route a consumer must call before trusting any other: it decides whether the artifact it
// just downloaded is an ABI generation it knows.
const encoded = cna._cna_get_abi_version();
const major = (encoded >>> 16) & 0xff;
const minor = (encoded >>> 8) & 0xff;
const patch = encoded & 0xff;
console.log(`cna_get_abi_version() -> ${major}.${minor}.${patch} (0x${encoded.toString(16)})`);
if (encoded === 0) {
    console.error('the ABI version came back zero; the call did not reach compiled code');
    exit(1);
}

// A second route, taking a pointer and writing through it, so the check covers more than a
// constant return. Deliberately a device-free one: this proves the ABI is callable, not that a
// browserless Node process can bring up a renderer.
const out = cna._malloc(8);
try {
    cna.HEAPU32[out >> 2] = 0;
    cna.HEAPU32[(out >> 2) + 1] = 0;
    const result = cna._cna_platform_get_current_name_size_ext(out);
    const bytes = cna.HEAPU32[out >> 2];
    console.log(`cna_platform_get_current_name_size_ext() -> result=${result} bytes=${bytes}`);
    if (result !== 0 || bytes === 0) {
        console.error('the platform-name route did not answer');
        exit(1);
    }
} finally {
    cna._free(out);
}

// The browser build emulates the Windows Phone input environment: it has no physical browser
// sensor backend, so canonical phone samples must take their emulator-input branch. Exercise the
// C route, not only the C++ Environment property, so bindings cannot observe a different identity.
const deviceTypeOut = cna._malloc(4);
try {
    cna.HEAPU32[deviceTypeOut >> 2] = 0xffffffff;
    const result = cna._cna_environment_get_device_type(deviceTypeOut);
    const deviceType = cna.HEAPU32[deviceTypeOut >> 2];
    console.log(`cna_environment_get_device_type() -> result=${result} type=${deviceType}`);
    if (result !== 0 || deviceType !== 1) {
        console.error('the Web target did not report CNA_DEVICE_TYPE_EMULATOR');
        exit(1);
    }
} finally {
    cna._free(deviceTypeOut);
}

// Completeness: every generated name must actually be on the module.
if (exportsPath) {
    const declared = JSON.parse(readFileSync(exportsPath, 'utf8'));
    const names = Array.isArray(declared) ? declared : declared.exports;
    const missing = names.filter((name) => typeof cna[name] !== 'function');
    console.log(`export completeness: ${names.length - missing.length}/${names.length} present`);
    if (missing.length > 0) {
        console.error(
            `the module is missing ${missing.length} declared export(s); the generated list and ` +
            'the linked module disagree. First few: ' + missing.slice(0, 10).join(', '));
        exit(1);
    }
} else {
    console.error('no exports json given, so export completeness was NOT checked');
    exit(2);
}

console.log('wasm module smoke: OK');
