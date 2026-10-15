# SPDX-FileCopyrightText: 2026 MrReSc
# SPDX-License-Identifier: Apache-2.0

import os
import zipfile

top = '.'
out = 'build'


def include_distribution_notices(ctx):
    bundle = ctx.bldnode.make_node(ctx.env.BUNDLE_NAME).abspath()
    paths = [
        'LICENSE',
        'PRIVACY.md',
        'THIRD_PARTY_NOTICES.md',
        'LICENSES/DigitalNumbers-OFL-1.1.txt',
        'LICENSES/SunCalc-BSD-2-Clause.txt',
    ]
    notices = {path: ctx.path.find_node(path).read('rb') for path in paths}
    with zipfile.ZipFile(bundle) as archive:
        if all(path in archive.namelist() and archive.read(path) == data
               for path, data in notices.items()):
            return
        entries = [(info, archive.read(info.filename)) for info in archive.infolist()
                   if info.filename not in notices]
    temporary = bundle + '.tmp'
    with zipfile.ZipFile(temporary, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for info, data in entries:
            archive.writestr(info, data)
        for path, data in notices.items():
            archive.writestr(path, data)
    os.replace(temporary, bundle)


def options(ctx):
    ctx.load('pebble_sdk')


def configure(ctx):
    ctx.load('pebble_sdk')


def build(ctx):
    ctx.load('pebble_sdk')
    binaries = []
    cached_env = ctx.env
    for platform in ctx.env.TARGET_PLATFORMS:
        ctx.env = ctx.all_envs[platform]
        ctx.set_group(ctx.env.PLATFORM_NAME)
        app_elf = '{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
        ctx.pbl_build(source=ctx.path.ant_glob('src/c/**/*.c'),
                      target=app_elf, bin_type='app')
        binaries.append({'platform': platform, 'app_elf': app_elf})
    ctx.env = cached_env
    ctx.set_group('bundle')
    ctx.pbl_bundle(binaries=binaries,
                   js=ctx.path.ant_glob(['src/pkjs/**/*.js',
                                         'src/pkjs/**/*.json']),
                   js_entry_file='src/pkjs/index.js')
    ctx.add_post_fun(include_distribution_notices)
