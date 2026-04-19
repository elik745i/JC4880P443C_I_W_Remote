#include <string.h>
#include "audio_log.h"
#include "audio_mp3.h"

static const char *TAG = "mp3";

static const char *mp3_error_to_string(int error)
{
    switch (error) {
    case ERR_MP3_NONE:
        return "no error";
    case ERR_MP3_INDATA_UNDERFLOW:
        return "input underflow";
    case ERR_MP3_MAINDATA_UNDERFLOW:
        return "main data underflow";
    case ERR_MP3_FREE_BITRATE_SYNC:
        return "free bitrate sync";
    case ERR_MP3_OUT_OF_MEMORY:
        return "out of memory";
    case ERR_MP3_NULL_POINTER:
        return "null pointer";
    case ERR_MP3_INVALID_FRAMEHEADER:
        return "invalid frame header";
    case ERR_MP3_INVALID_SIDEINFO:
        return "invalid side info";
    case ERR_MP3_INVALID_SCALEFACT:
        return "invalid scalefactor";
    case ERR_MP3_INVALID_HUFFCODES:
        return "invalid huffman codes";
    case ERR_MP3_INVALID_DEQUANTIZE:
        return "invalid dequantize";
    case ERR_MP3_INVALID_IMDCT:
        return "invalid IMDCT";
    case ERR_MP3_INVALID_SUBBAND:
        return "invalid subband";
    default:
        return "unknown";
    }
}

static bool probe_mp3_frame(FILE *fp, long start_offset)
{
    constexpr size_t kProbeBytes = 2048;
    uint8_t probe[kProbeBytes] = {};

    if (fseek(fp, start_offset, SEEK_SET) != 0) {
        return false;
    }

    const size_t bytes_read = fread(probe, 1, sizeof(probe), fp);
    if (bytes_read < 4) {
        return false;
    }

    const int sync_offset = MP3FindSyncWord(probe, static_cast<int>(bytes_read));
    if (sync_offset < 0) {
        return false;
    }

    MP3FrameInfo frame_info = {};
    return MP3GetNextFrameInfo(nullptr, &frame_info, probe + sync_offset) == ERR_MP3_NONE;
}

bool is_mp3(FILE *fp) {
    bool is_mp3_file = false;

    fseek(fp, 0, SEEK_SET);

    // see https://en.wikipedia.org/wiki/List_of_file_signatures
    uint8_t magic[3];
    if(sizeof(magic) == fread(magic, 1, sizeof(magic), fp)) {
        if((magic[0] == 0xFF) &&
            (magic[1] == 0xFB))
        {
            is_mp3_file = true;
        } else if((magic[0] == 0xFF) &&
                  (magic[1] == 0xF3))
        {
            is_mp3_file = true;
        } else if((magic[0] == 0xFF) &&
                  (magic[1] == 0xF2))
        {
            is_mp3_file = true;
        } else if((magic[0] == 0x49) &&
                  (magic[1] == 0x44) &&
                  (magic[2] == 0x33)) /* 'ID3' */
        {
            fseek(fp, 0, SEEK_SET);

            /* Get ID3 head */
            mp3_id3_header_v2_t tag;
            if (sizeof(mp3_id3_header_v2_t) == fread(&tag, 1, sizeof(mp3_id3_header_v2_t), fp)) {
                if (memcmp("ID3", (const void *) &tag, sizeof(tag.header)) == 0) {
                    const long tag_size =
                        ((tag.size[0] & 0x7F) << 21) |
                        ((tag.size[1] & 0x7F) << 14) |
                        ((tag.size[2] & 0x7F) << 7) |
                        (tag.size[3] & 0x7F);
                    is_mp3_file = probe_mp3_frame(fp, static_cast<long>(sizeof(mp3_id3_header_v2_t)) + tag_size);
                }
            }
        }
    }

    if (!is_mp3_file &&
        (magic[0] == 0xFF) && ((magic[1] == 0xFB) || (magic[1] == 0xF3) || (magic[1] == 0xF2))) {
        is_mp3_file = probe_mp3_frame(fp, 0);
    }

    // seek back to the start of the file to avoid
    // missing frames upon decode
    fseek(fp, 0, SEEK_SET);

    return is_mp3_file;
}

/**
 * @return true if data remains, false on error or end of file
 */
DECODE_STATUS decode_mp3(HMP3Decoder mp3_decoder, FILE *fp, decode_data *pData, mp3_instance *pInstance) {
    MP3FrameInfo frame_info;

    size_t unread_bytes = pInstance->bytes_in_data_buf - (pInstance->read_ptr - pInstance->data_buf);

    /* somewhat arbitrary trigger to refill buffer - should always be enough for a full frame */
    if (unread_bytes < 1.25 * MAINBUF_SIZE && !pInstance->eof_reached) {
        uint8_t *write_ptr = pInstance->data_buf + unread_bytes;
        size_t free_space = pInstance->data_buf_size - unread_bytes;

    	/* move last, small chunk from end of buffer to start,
           then fill with new data */
        memmove(pInstance->data_buf, pInstance->read_ptr, unread_bytes);

        size_t nRead = fread(write_ptr, 1, free_space, fp);

        pInstance->bytes_in_data_buf = unread_bytes + nRead;
        pInstance->read_ptr = pInstance->data_buf;

        if ((nRead == 0) || feof(fp)) {
            pInstance->eof_reached = true;
        }

        LOGI_2("pos %ld, nRead %d, eof %d", ftell(fp), nRead, pInstance->eof_reached);

        unread_bytes = pInstance->bytes_in_data_buf;
    }

    LOGI_3("data_buf 0x%p, read 0x%p", pInstance->data_buf, pInstance->read_ptr);

    if(unread_bytes == 0) {
        LOGI_1("unread_bytes == 0, status done");
        return DECODE_STATUS_DONE;
    }

    /* Find MP3 sync word from read buffer */
    int offset = MP3FindSyncWord(pInstance->read_ptr, unread_bytes);

    LOGI_2("unread %d, total %d, offset 0x%x(%d)",
            unread_bytes, pInstance->bytes_in_data_buf, offset, offset);

    if (offset >= 0) {
        COMPILE_3(int starting_unread_bytes = unread_bytes);
        uint8_t *frame_ptr = pInstance->read_ptr + offset; /*!< Data start point */
        uint8_t *read_ptr = frame_ptr;
        unread_bytes -= offset;
        LOGI_3("read 0x%p, unread %d", read_ptr, unread_bytes);
        int mp3_dec_err = MP3Decode(mp3_decoder, &read_ptr, (int*)&unread_bytes, reinterpret_cast<int16_t *>(pData->samples), 
0);

        pInstance->read_ptr = read_ptr;

        if(mp3_dec_err == ERR_MP3_NONE) {
            /* Get MP3 frame info */
            MP3GetLastFrameInfo(mp3_decoder, &frame_info);

            pData->fmt.sample_rate = frame_info.samprate;
            pData->fmt.bits_per_sample = frame_info.bitsPerSample;
            pData->fmt.channels = frame_info.nChans;

            pData->frame_count = (frame_info.outputSamps / frame_info.nChans);

            LOGI_3("mp3: channels %d, sr %d, bps %d, frame_count %d, processed %d",
                pData->fmt.channels,
                pData->fmt.sample_rate,
                pData->fmt.bits_per_sample,
                frame_info.outputSamps,
                starting_unread_bytes - unread_bytes);
        } else {
            if (pInstance->eof_reached) {
                ESP_LOGW(TAG, "decode ended at EOF after %s (%d)", mp3_error_to_string(mp3_dec_err), mp3_dec_err);
                return DECODE_STATUS_DONE;
            } else if (mp3_dec_err == ERR_MP3_MAINDATA_UNDERFLOW) {
                // underflow indicates MP3Decode should be called again
                LOGI_1("underflow read ptr is 0x%p", read_ptr);
                return DECODE_STATUS_NO_DATA_CONTINUE;
            } else {
                size_t bytes_consumed = static_cast<size_t>(read_ptr - frame_ptr);
                size_t bytes_to_skip = (bytes_consumed > 0) ? bytes_consumed : 1;
                pInstance->read_ptr = frame_ptr + bytes_to_skip;
                ESP_LOGW(TAG, "skipping %u byte after %s (%d)",
                         (unsigned)bytes_to_skip, mp3_error_to_string(mp3_dec_err), mp3_dec_err);
                return DECODE_STATUS_NO_DATA_CONTINUE;
            }
        }
    } else {
        // if we are dropping data there were no frames decoded
        pData->frame_count = 0;

        // drop an even count of words
        size_t words_to_drop = unread_bytes / BYTES_IN_WORD;
        size_t bytes_to_drop = words_to_drop * BYTES_IN_WORD;

        // if the unread bytes is less than BYTES_IN_WORD, we should drop any unread bytes
        // to avoid the situation where the file could have a few extra bytes at the end
        // of the file that isn't at least BYTES_IN_WORD and decoding would get stuck
        if(unread_bytes < BYTES_IN_WORD) {
            bytes_to_drop = unread_bytes;
        }

        // shift the read_ptr to drop the bytes in the buffer
        pInstance->read_ptr += bytes_to_drop;

        /* Sync word not found in frame. Drop data that was read until a word boundary */
        ESP_LOGE(TAG, "MP3 sync word not found, dropping %d bytes", bytes_to_drop);
    }

    return DECODE_STATUS_CONTINUE;
}
