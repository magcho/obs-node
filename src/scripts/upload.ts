import * as util from 'util';
import * as os from 'os';
import * as tar from 'tar-fs';
import * as fs from 'fs';
import * as path from 'path';
import * as stream from 'stream';
import * as OSS from "ali-oss";

const pipeline = util.promisify(stream.pipeline);
const region = process.env.OSS_REGION;
const accessKey = process.env.OSS_ACCESS_KEY;
const secretKey = process.env.OSS_SECRET_KEY;
const bucket = process.env.OSS_BUCKET;
if (!region || !accessKey || !secretKey) {
    throw new Error(`OSS_REGION, OSS_ACCESS_KEY, or OSS_SECRET_KEY env is not provided.`);
}

const packageJson = JSON.parse(fs.readFileSync(path.resolve(__dirname, '../../package.json'), 'utf8'));
const version = packageJson.version;
const platform = os.platform();
const tarFileName = `prebuild-${platform}-${version}.tar.gz`;
const oss = new OSS({
    region: region,
    accessKeyId: accessKey,
    accessKeySecret: secretKey,
    bucket: bucket,
});

(async () => {
    // create prebuild tar
    const prebuildFolder = path.resolve(__dirname, '../../prebuild');
    await pipeline(tar.pack(prebuildFolder), fs.createWriteStream(tarFileName));

    // upload tar file to oss
    await oss.put(`obs-node/${tarFileName}`, tarFileName);

    // delete tar file
    fs.unlinkSync(tarFileName);
})();