package org.afedecomp.dobutsunomori;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/* Reads the parts of an Android tombstone (the protobuf the system keeps for
 * a native crash, ApplicationExitInfo.getTraceInputStream on Android 12+)
 * that say what happened: the signal, the crashing thread's call stack and
 * the process's last log lines. Field numbers are those of AOSP's
 * system/core/debuggerd/proto/tombstone.proto. */
final class Tombstone {
    private Tombstone() {
    }

    /* Protobuf wire format, only what the tombstone needs */
    private static final class Reader {
        private final byte[] buf;
        private int pos;
        private final int end;
        int field;
        int wire;

        Reader(byte[] buf, int start, int end) {
            this.buf = buf;
            this.pos = start;
            this.end = Math.min(end, buf.length);
        }

        boolean next() {
            if (pos >= end) {
                return false;
            }
            long tag = varint();
            field = (int) (tag >>> 3);
            wire = (int) (tag & 7);
            return true;
        }

        long varint() {
            long value = 0;
            int shift = 0;

            while (pos < end && shift < 64) {
                int b = buf[pos++] & 0xff;

                value |= (long) (b & 0x7f) << shift;
                if (b < 0x80) {
                    break;
                }
                shift += 7;
            }
            return value;
        }

        private int length() {
            long len = varint();

            if (len < 0 || len > end - pos) {
                return end - pos;
            }
            return (int) len;
        }

        Reader message() {
            int len = length();
            Reader r = new Reader(buf, pos, pos + len);

            pos += len;
            return r;
        }

        String string() {
            int len = length();
            String s = new String(buf, pos, len, StandardCharsets.UTF_8);

            pos += len;
            return s;
        }

        void skip() {
            switch (wire) {
            case 0:
                varint();
                break;
            case 1:
                pos += 8;
                break;
            case 2: {
                int len = length(); /* not pos += length(): that adds to pos as it was before the length */

                pos += len;
                break;
            }
            case 5:
                pos += 4;
                break;
            default:
                pos = end;
                break;
            }
        }
    }

    private static final class Frame {
        long relPc;
        String function = "";
        long functionOffset;
        String file = "";
    }

    private static final class ThreadInfo {
        int id;
        String name = "";
        List<Frame> frames = new ArrayList<Frame>();
    }

    static String describe(byte[] data) {
        try {
            return parse(data);
        } catch (Throwable e) {
            return "(tombstone: " + e + ")\n";
        }
    }

    private static String parse(byte[] data) {
        Reader r = new Reader(data, 0, data.length);
        StringBuilder out = new StringBuilder();
        List<ThreadInfo> threads = new ArrayList<ThreadInfo>();
        List<String> causes = new ArrayList<String>();
        List<String> logs = new ArrayList<String>();
        String signal = "";
        String abort = "";
        int tid = -1;

        while (r.next()) {
            if (r.field == 6 && r.wire == 0) {
                tid = (int) r.varint();
            } else if (r.field == 10 && r.wire == 2) {
                signal = signal(r.message());
            } else if (r.field == 14 && r.wire == 2) {
                abort = r.string();
            } else if (r.field == 15 && r.wire == 2) {
                Reader c = r.message();

                while (c.next()) {
                    if (c.field == 1 && c.wire == 2) {
                        causes.add(c.string());
                    } else {
                        c.skip();
                    }
                }
            } else if (r.field == 16 && r.wire == 2) {
                /* map<uint32, Thread>: entries of key = 1, value = 2 */
                Reader entry = r.message();

                while (entry.next()) {
                    if (entry.field == 2 && entry.wire == 2) {
                        threads.add(thread(entry.message()));
                    } else {
                        entry.skip();
                    }
                }
            } else if (r.field == 18 && r.wire == 2) {
                logBuffer(r.message(), logs);
            } else {
                r.skip();
            }
        }

        out.append(signal);
        if (!abort.isEmpty()) {
            out.append("abort message: ").append(abort).append('\n');
        }
        for (String cause : causes) {
            out.append("cause: ").append(cause).append('\n');
        }
        for (ThreadInfo t : threads) {
            if (t.id != tid) {
                continue;
            }
            out.append("thread ").append(t.id).append(" \"").append(t.name).append("\":\n");
            for (int i = 0; i < t.frames.size() && i < 48; i++) {
                Frame f = t.frames.get(i);
                String file = f.file;
                int slash = file.lastIndexOf('/');

                if (slash >= 0) {
                    file = file.substring(slash + 1);
                }
                out.append(String.format(Locale.ROOT, "  #%02d %s+0x%x", i, file, f.relPc));
                if (!f.function.isEmpty()) {
                    out.append(" (").append(f.function).append('+').append(f.functionOffset).append(')');
                }
                out.append('\n');
            }
        }
        if (!logs.isEmpty()) {
            out.append("--- logcat (this process) ---\n");
            for (int i = Math.max(0, logs.size() - 40); i < logs.size(); i++) {
                out.append(logs.get(i)).append('\n');
            }
        }
        return out.toString();
    }

    private static String signal(Reader r) {
        String name = "";
        String codeName = "";
        int number = 0;
        int code = 0;
        boolean hasFault = false;
        long fault = 0;

        while (r.next()) {
            switch (r.field) {
            case 1:
                number = (int) r.varint();
                break;
            case 2:
                name = r.string();
                break;
            case 3:
                code = (int) r.varint();
                break;
            case 4:
                codeName = r.string();
                break;
            case 8:
                hasFault = r.varint() != 0;
                break;
            case 9:
                fault = r.varint();
                break;
            default:
                r.skip();
                break;
            }
        }
        StringBuilder s = new StringBuilder();
        s.append("signal ").append(number).append(" (").append(name).append("), code ").append(code)
            .append(" (").append(codeName).append(')');
        if (hasFault) {
            s.append(String.format(Locale.ROOT, ", fault addr 0x%x", fault));
        }
        return s.append('\n').toString();
    }

    private static ThreadInfo thread(Reader r) {
        ThreadInfo t = new ThreadInfo();

        while (r.next()) {
            if (r.field == 1 && r.wire == 0) {
                t.id = (int) r.varint();
            } else if (r.field == 2 && r.wire == 2) {
                t.name = r.string();
            } else if (r.field == 4 && r.wire == 2) {
                Reader fr = r.message();
                Frame f = new Frame();

                while (fr.next()) {
                    if (fr.field == 1 && fr.wire == 0) {
                        f.relPc = fr.varint();
                    } else if (fr.field == 4 && fr.wire == 2) {
                        f.function = fr.string();
                    } else if (fr.field == 5 && fr.wire == 0) {
                        f.functionOffset = fr.varint();
                    } else if (fr.field == 6 && fr.wire == 2) {
                        f.file = fr.string();
                    } else {
                        fr.skip();
                    }
                }
                t.frames.add(f);
            } else {
                r.skip();
            }
        }
        return t;
    }

    private static void logBuffer(Reader r, List<String> logs) {
        while (r.next()) {
            if (r.field == 2 && r.wire == 2) {
                Reader m = r.message();
                String time = "";
                String tag = "";
                String message = "";
                int priority = 0;

                while (m.next()) {
                    if (m.field == 1 && m.wire == 2) {
                        time = m.string();
                    } else if (m.field == 4 && m.wire == 0) {
                        priority = (int) m.varint();
                    } else if (m.field == 5 && m.wire == 2) {
                        tag = m.string();
                    } else if (m.field == 6 && m.wire == 2) {
                        message = m.string();
                    } else {
                        m.skip();
                    }
                }
                /* android_LogPriority: 2 V, 3 D, 4 I, 5 W, 6 E, 7 F */
                char level = priority >= 2 && priority <= 7 ? "VDIWEF".charAt(priority - 2) : '?';
                logs.add(time + " " + level + "/" + tag + ": " + message.trim());
            } else {
                r.skip();
            }
        }
    }
}
