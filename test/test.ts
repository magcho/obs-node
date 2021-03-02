import * as obs from '../src';
import * as readline from 'readline';
import {SourceSettings} from "../src";
import * as fs from "fs";

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
    outputs: [
        {
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
            audioBitrateKbps: 64
        },
    ],
};

const dsks = [
    {
        id: "dsk1",
        position: "top-left",
        url: "",
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
            isFile: false,
            url: 'rtmp://host.docker.internal/live/source1',
            hardwareDecoder: false,
            startOnActive: false,
        }
    },
    {
        sceneId: 'scene2',
        sourceId: 'source2',
        settings: {
            type: 'MediaSource',
            isFile: true,
            url: 'test.mp4',
            hardwareDecoder: false,
            startOnActive: false,
            output: {
                server: 'rtmp://host.docker.internal/preview',
                key: 'source2',
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
    }
];

obs.startup(settings);

sources.forEach(s => {
    obs.addScene(s.sceneId);
    obs.addSource(s.sceneId, s.sourceId, s.settings);
});

dsks.forEach(dsk => {
   obs.addDSK(dsk.id, dsk.position as obs.Position, dsk.url, dsk.left, dsk.top, dsk.width, dsk.height);
});

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

question(async sceneId => {
    if (sceneId.startsWith('screenshot '))  {
        sceneId = sceneId.replace('screenshot ', '');
        const source = sources.find(s => s.sceneId === sceneId);
        const buffer = await obs.screenshot(source.sceneId, source.sourceId);
        fs.writeFileSync('screenshot.png', buffer);
    } else {
        obs.switchToScene(sceneId, 'cut_transition', 1000);
    }
});