// SPDX-License-Identifier: MS-PL
//
// plans/plan_cabi.md CABI-14: load the built module and call into it.
//
// The structural checks (an ESM factory, 2871 wrappers matching the generated list) only prove the
// link was configured correctly. This proves the module instantiates and that a call reaches
// compiled C and comes back with the right answer, which is the first thing cna-ts needs to trust.

import { argv, exit } from 'node:process';

const modulePath = argv[2];
if (!modulePath) {
    console.error('usage: module_smoke.mjs <path to cna_c_api.mjs>');
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

console.log('wasm module smoke: OK');
