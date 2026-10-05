from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from upscale_batch import Batch, canvas_size, pending_frames, valid_png, video_filter


def png(width, height):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind+data))
    header = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
    pixels = zlib.compress((b'\0' + bytes(width*3)) * height)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) + chunk(b'IDAT', pixels) + chunk(b'IEND', b'')


class BatchTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)

    def test_tall_movies_and_portal_are_never_cropped_or_resized(self):
        self.assertEqual(canvas_size(640, 320), (3840, 2160))
        self.assertEqual(canvas_size(640, 448), (3840, 2688))
        self.assertEqual(canvas_size(128, 144), (768, 864))
        self.assertEqual(video_filter((3840, 2688), (3840, 2688)), 'setsar=1')
        self.assertNotIn('scale', video_filter((3840, 1920), (3840, 2160)))

    def test_resume_rejects_truncated_corrupt_and_wrong_sized_frames(self):
        complete = png(16, 16)
        (self.root/'frame000001.png').write_bytes(complete)
        (self.root/'frame000002.png').write_bytes(complete[:-10])
        corrupt = bytearray(complete);corrupt[45] ^= 1
        (self.root/'frame000003.png').write_bytes(corrupt)
        (self.root/'frame000004.png').write_bytes(png(32, 16))
        self.assertEqual(pending_frames(self.root, 5, (16,16)),
                         ['frame000002.png', 'frame000003.png', 'frame000004.png', 'frame000005.png'])

    def test_ai_resume_processes_only_missing_frames_and_retains_completed_output(self):
        batch = Batch.__new__(Batch)
        batch.config = {'realesrgan': 'ai', 'models': 'models'}
        batch.job_report = self.root/'job.json'
        source, output = self.root/'input', self.root/'output'
        source.mkdir();output.mkdir()
        for i in (1,2,3):
            (source/f'frame{i:06d}.png').write_bytes(png(16,16))
        done = output/'frame000001.png';done.write_bytes(png(32,32))
        before = done.stat().st_mtime_ns
        (output/'frame000002.png').write_bytes(b'partial')
        seen = []
        def finish(phase, command, job, progress_folder):
            inputs = Path(command[command.index('-i')+1])
            seen.extend(sorted(p.name for p in inputs.iterdir()))
            for path in inputs.iterdir():
                (output/path.name).write_bytes(png(32,32))
        batch.run = finish
        batch.ai_pass(source, output, 3, (32,32), 2, {})
        self.assertEqual(seen, ['frame000002.png','frame000003.png'])
        self.assertEqual(done.stat().st_mtime_ns, before)
        self.assertFalse((self.root/'pending-2x').exists())
        seen.clear()
        batch.ai_pass(source, output, 3, (32,32), 2, {})
        self.assertEqual(seen, [])

    def test_failed_ai_retains_finished_frames_and_removes_pending_links(self):
        batch = Batch.__new__(Batch)
        batch.config = {'realesrgan': 'ai', 'models': 'models'}
        batch.job_report = self.root/'job.json'
        source, output = self.root/'input', self.root/'output'
        source.mkdir();output.mkdir()
        for i in (1,2):(source/f'frame{i:06d}.png').write_bytes(png(16,16))
        def partial(*args):
            (output/'frame000001.png').write_bytes(png(32,32))
            raise KeyboardInterrupt
        batch.run = partial
        with self.assertRaises(KeyboardInterrupt):batch.ai_pass(source, output, 2, (32,32), 2, {})
        self.assertTrue(valid_png(output/'frame000001.png', (32,32)))
        self.assertFalse((self.root/'pending-2x').exists())
        self.assertTrue((source/'frame000001.png').exists())

    def test_publish_never_overwrites_a_different_preview(self):
        batch = Batch.__new__(Batch)
        batch.publish = self.root/'published';batch.publish.mkdir()
        batch.minimum_free = 0
        movie = self.root/'scene.mp4';movie.write_bytes(b'new preview')
        previous = batch.publish/movie.name;previous.write_bytes(b'previous preview')
        with self.assertRaisesRegex(ValueError, 'different movie'):batch.publish_movie(movie)
        self.assertEqual(previous.read_bytes(), b'previous preview')
        previous.unlink();batch.publish_movie(movie)
        self.assertEqual(previous.read_bytes(), movie.read_bytes())
        self.assertEqual(previous.stat().st_mode & 0o777, 0o644)

    def test_decode_requires_complete_frame_count_not_just_successful_exit(self):
        batch = Batch.__new__(Batch);batch.config = {'ffmpeg':'ffmpeg'}
        batch.job_report = self.root/'job.json'
        def short_decode(*args):
            (self.root/'verify-progress.txt').write_text('frame=2\nprogress=end\n')
        batch.run = short_decode
        with self.assertRaisesRegex(ValueError, 'expected 3'):batch.verify(self.root/'scene.mp4', 3, {})


if __name__ == '__main__':
    unittest.main()
