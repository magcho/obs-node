import * as obs from '../src';
import * as readline from 'readline';
import {CG, CGImage, CGText, Overlay, SourceSettings} from "../src";
import * as fs from "fs";
import {OutputSettings} from "../dist";

function as<T>(value: T): T {
    return value;
}

interface Source {
    sceneId: string;
    sourceId: string;
    settings: SourceSettings;
}

interface Output {
    id: string;
    settings: OutputSettings;
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
};

const outputs: Output[] = [
        {
            id: 'output1',
            settings: {
                url: 'rtmp://host.docker.internal/live/output',
                hardwareEnable: false,
                width: 640,
                height: 360,
                keyintSec: 5,
                rateControl: 'CBR',
                preset: 'ultrafast',
                profile: 'main',
                tune: 'zerolatency',
                videoBitrateKbps: 1000,
                audioBitrateKbps: 64,
            }
        },
        {
            id: 'preview',
            settings: {
                url: 'rtmp://host.docker.internal/preview/output',
                hardwareEnable: false,
                width: 320,
                height: 180,
                keyintSec: 5,
                rateControl: 'CBR',
                preset: 'ultrafast',
                profile: 'main',
                tune: 'zerolatency',
                videoBitrateKbps: 1000,
                audioBitrateKbps: 64,
            }
        },
    ];

const overlays: Overlay[] = [
    as<CG>({
        id: 'cg1',
        name: 'cg1',
        type: 'cg',
        baseWidth: 960,
        baseHeight: 540,
        items: [
            as<CGImage>({
                type: 'image',
                x: 0,
                y: 0,
                width: 100,
                height: 100,
                url: 'https://httpbin.org/image/png',
            }),
            as<CGText>({
                type: 'text',
                x: 0,
                y: 0,
                width: 100,
                height: 100,
                content: 'MENG LI',
                fontSize: 40,
                fontFamily: 'SimSun',
                colorABGR: 'ffff0000',
            }),
        ]
    }),
];

const sources: Source[] = [
    {
        sceneId: 'scene1',
        sourceId: 'source1',
        settings: {
            name: 'source1',
            type: 'live',
            url: 'rtmp://host.docker.internal/live/source1',
            hardwareDecoder: false,
            playOnActive: false,
        }
    },
    {
        sceneId: 'scene2',
        sourceId: 'source2',
        settings: {
            name: 'source1',
            type: 'media',
            url: 'clips/test.mp4',
            hardwareDecoder: false,
            playOnActive: false,
            output: {
                url: 'rtmp://host.docker.internal/preview/source2',
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

outputs.forEach(o => obs.addOutput(o.id, o.settings));

overlays.forEach(overlay => obs.addOverlay(overlay));

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
    } else if (sceneId.startsWith('upOverlay ')) {
        const overlayId = sceneId.replace('upOverlay ', '');
        obs.upOverlay(overlayId);
    } else if (sceneId.startsWith('downOverlay ')) {
        const overlayId = sceneId.replace('downOverlay ', '');
        obs.downOverlay(overlayId);
    } else if (sceneId.startsWith('removeScene ')) {
        const id = sceneId.replace('removeScene ', '');
        obs.removeScene(id);
    } else {
        obs.switchToScene(sceneId, 'cut_transition', 1000);
    }
});