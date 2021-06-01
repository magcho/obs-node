import * as os from 'os';
import * as path from 'path';

let obs: obs.ObsNode;
let cwd = process.cwd();
const isWindows = os.platform() === "win32";
try {
    if (isWindows) {
        // for windows, we need set working directory to obs binary path to load obs dependencies correctly.
        process.chdir(path.resolve(__dirname, '../prebuild/obs-studio/bin/64bit'));
    }
    obs = require('../prebuild/obs-node.node');
} finally {
    if (isWindows) {
        process.chdir(cwd);
    }
}

// set obs studio path before calling any function.
const obsPath = path.resolve(__dirname, '../prebuild/obs-studio');
obs.setObsPath(obsPath);

declare namespace obs {

    export type RateControl = 'CBR' | 'VBR';

    export type SourceType = 'live' | 'media';

    export type Position = 'top' | 'top-right' | 'right' | 'bottom-right' | 'bottom' | 'bottom-left' | 'left' | 'top-left' | 'center';

    export type TransitionType = 'cut_transition' | 'fade_transition' | 'swipe_transition' | 'slide_transition';

    export type AudioMode = 'follow' | 'standalone';

    export type OverlayType = 'cg';

    export type OverlayStatus = 'up' | 'down';

    export type CGItemType = 'image' | 'text';

    export interface VideoSettings {
        baseWidth: number;
        baseHeight: number;
        outputWidth: number;
        outputHeight: number;
        fpsNum: number;
        fpsDen: number;
    }

    export interface AudioSettings {
        sampleRate: number;
    }

    export interface OutputSettings {
        url: string;
        hardwareEnable: boolean;
        width: number;
        height: number;
        keyintSec: number;
        rateControl: RateControl;
        preset: string;
        profile: string;
        tune: string;
        x264opts?: string;
        videoBitrateKbps: number;
        audioBitrateKbps: number;
    }

    export interface Settings {
        locale?: string;
        fontDirectory?: string;
        showTimestamp?: boolean;
        timestampFontPath?: string;
        video: VideoSettings;
        audio: AudioSettings;
    }

    export interface SourceSettings {
        name: string;
        type: SourceType;
        url: string;
        hardwareDecoder?: boolean;
        playOnActive?: boolean;
        volume?: number;
        monitor?: boolean;
        audioLock?: boolean;
        asyncUnbuffered?: boolean;
        bufferingMb?: number;
        reconnectDelaySec?: number;
        output?: OutputSettings | null;
    }

    export type Source = {
        id: string;
        sceneId: string;
    } & SourceSettings;

    export interface Audio {
        volume: number;
        mode: AudioMode;
    }

    export type VolmeterCallback = (
        sceneId: string,
        sourceId: string,
        channels: number,
        magnitude: number[],
        peak: number[],
        input_peak: number[]) => void;

    export interface Overlay {
        id: string;
        name: string;
        type: OverlayType;
        status?: OverlayStatus;
    }

    export interface CG extends Overlay {
        baseWidth: number;
        baseHeight: number;
        items: CGItem[];
    }

    export interface CGItem {
        type: CGItemType;
        x: number;
        y: number;
        width: number;
        height: number;
    }

    export interface CGText extends CGItem {
        content: string;
        fontSize: number;
        fontFamily: string;
        colorABGR: string;
    }

    export interface CGImage extends CGItem {
        url: string;
    }

    export interface ObsNode {
        setObsPath(obsPath: string): void
        startup(settings: Settings): void;
        shutdown(): void;
        addScene(sceneId: string): string;
        removeScene(sceneId: string): void;
        addSource(sceneId: string, sourceId: string, settings: SourceSettings): void;
        getSource(sceneId: string, sourceId: string): Source;
        getSourceServerTimestamp(sceneId: string, sourceId: string): string;
        updateSource(sceneId: string, sourceId: string, settings: Partial<SourceSettings>): void;
        restartSource(sceneId: string, sourceId: string): void;
        switchToScene(sceneId: string, transitionType: TransitionType, transitionMs: number, timestamp?: string): void;
        addOutput(outputId: string, settings: OutputSettings);
        updateOutput(outputId: string, settings: OutputSettings);
        removeOutput(outputId: string);
        createDisplay(name: string, parentWindow: Buffer, scaleFactor: number, sourceIds: string[]): void;
        destroyDisplay(name: string): void;
        moveDisplay(name: string, x: number, y: number, width: number, height: number): void;
        addVolmeterCallback(callback: VolmeterCallback): void;
        getAudio(): Audio;
        updateAudio(audio: Partial<Audio>): void;
        screenshot(sceneId: string, sourceId: string): Promise<Buffer>;
        addOverlay(overlay: Overlay): void;
        removeOverlay(overlayId: string): void;
        upOverlay(overlayId: string): void;
        downOverlay(overlayId: string): void;
        getOverlays(): Overlay[];
    }
}

export = obs;