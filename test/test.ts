import * as obs from '../src';
import * as readline from 'readline';
import {SourceSettings} from "../src";

interface Source {
    sceneId: string;
    sourceId: string;
    settings: SourceSettings;
}

const settings: obs.Settings = {
    video: {
        baseWidth: 1280,
        baseHeight: 720,
        outputWidth: 1280,
        outputHeight: 720,
        fpsNum: 25,
        fpsDen: 1,
    },
    audio: {
        sampleRate: 44100,
    },
    output: {
        server: 'rtmp://host.docker.internal/live',
        key: 'output',
        hardwareEnable: false,
        width: 1280,
        height: 720,
        keyintSec: 1,
        rateControl: 'CBR',
        preset: 'ultrafast',
        profile: 'main',
        tune: 'zerolatency',
        videoBitrateKbps: 1000,
        audioBitrateKbps: 64,
    },
};

const dsks = [
    {
        id: "dsk1",
        position: "top-left",
        url: "https://httpbin.org/image/png",
        left: 100,
        top: 100,
        width: 256,
        height: 256,
    }
];

const sources: Source[] = [
    {
        sceneId: 'scene1',
        sourceId: 'source1',
        settings: {
            type: 'MediaSource',
            url: 'rtmp://host.docker.internal/live/source1',
            hardwareDecoder: false,
            output: {
                server: 'rtmp://host.docker.internal/preview',
                key: 'source1',
                hardwareEnable: false,
                width: 640,
                height: 360,
                keyintSec: 1,
                rateControl: 'CBR',
                preset: 'ultrafast',
                profile: 'baseline',
                tune: 'zerolatency',
                videoBitrateKbps: 1000,
                audioBitrateKbps: 64,
            }
        }
    },
    {
        sceneId: 'scene2',
        sourceId: 'source2',
        settings: {
            type: 'MediaSource',
            url: 'rtmp://host.docker.internal/live/source2',
            hardwareDecoder: false,
        }
    },
    {
        sceneId: 'scene3',
        sourceId: 'source3',
        settings: {
            type: 'MediaSource',
            url: 'rtmp://host.docker.internal/live/source3',
            hardwareDecoder: false,
        }
    }
];

obs.startup(settings);
// obs.addVolmeterCallback((sceneId: string,
//                          sourceId: string,
//                          channels: number,
//                          magnitude: number[],
//                          peak: number[],
//                          input_peak: number[]) => {
//     console.log(sceneId, sourceId, channels, magnitude, peak, input_peak);
// });

sources.forEach(s => {
    obs.addScene(s.sceneId);
    obs.addSource(s.sceneId, s.sourceId, s.settings);
});

dsks.forEach(dsk => {
   obs.addDSK(dsk.id, dsk.position as obs.Position, dsk.url, dsk.left, dsk.top, dsk.width, dsk.height);
});

console.log(`Source 1: ${JSON.stringify(obs.getSource(sources[0].sceneId, sources[0].sourceId))}`);
console.log(`Source 2: ${JSON.stringify(obs.getSource(sources[1].sceneId, sources[1].sourceId))}`);
console.log(`Audio: ${JSON.stringify(obs.getAudio())}`);

const readLine = readline.createInterface({
    input: process.stdin,
    output: process.stdout,
    terminal: true,
});

const question = (callback: (sceneId: string) => void) => {
    sources.forEach(s => readLine.write(`${s.sceneId}\n`));
    readLine.question('Which scene to switch?\n', async (sceneId: string) => {
        try {
            if (sceneId) {
                await callback(sceneId);
            }
        } finally {
            question(callback);
        }
    });
};

question(sceneId => {
    obs.switchToScene(sceneId, 'cut_transition', 1000);
});