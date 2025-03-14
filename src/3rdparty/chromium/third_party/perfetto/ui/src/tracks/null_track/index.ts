// Copyright (C) 2022 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

import {
  Plugin,
  PluginContext,
  PluginContextTrace,
  PluginDescriptor,
  Track,
} from '../../public';

export const NULL_TRACK_URI = 'perfetto.NullTrack';
export const NULL_TRACK_KIND = 'NullTrack';

export class NullTrack implements Track {
  getHeight(): number {
    return 30;
  }

  render(): void {}
}

class NullTrackPlugin implements Plugin {
  onActivate(_ctx: PluginContext): void {}

  async onTraceLoad(ctx: PluginContextTrace): Promise<void> {
    // TODO(stevegolton): This is not the right way to handle blank tracks,
    // instead we should probably just render some blank element at render time
    // if no track uri is supplied.
    ctx.registerTrack({
      uri: NULL_TRACK_URI,
      displayName: 'Null Track',
      kind: NULL_TRACK_KIND,
      track: () => new NullTrack(),
    });
  }
}

export const plugin: PluginDescriptor = {
  pluginId: 'perfetto.NullTrack',
  plugin: NullTrackPlugin,
};
