#include <math.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "all.h"

#define WIDTH 16
#define HEIGHT 16

const char *TAG = "web";
EXT_RAM_BSS_ATTR uint8_t framebuffer[WIDTH * HEIGHT * 3]; // RGB888 framebuffer
extern const char html_page[];

esp_err_t hello_get_handler(httpd_req_t *req)
{
    const char* resp = "Hello World";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}


// Handler for serving HTML page
esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_send(req, html_page, strlen(html_page));
    return ESP_OK;
}


float srgb_to_linear(float f) {
    if (f <= 0.04045f)
        return f / 12.92f;
    else
        return powf((f + 0.055f) / 1.055f, 2.4f);
}

/**
 * Process a 24-bit RGB buffer of 8-bit colors into a 6-bit color range, also adjusting for input
 * source.  Turns out the LED driver/board already seems to compenstate for the any driver power
 * as well as RGB differences so those are a nop.
 */
void process_image_6bit(uint8_t *fb, int fblen, bool srgb) {
    for (int ii = 0; ii < fblen; ii+=3) {
        float lr = fb[ii]   / 255.0f;  // 8bit to float
        float lg = fb[ii+1] / 255.0f;
        float lb = fb[ii+2] / 255.0f;

        if (srgb) {
            lr = srgb_to_linear(lr);
            lg = srgb_to_linear(lg);
            lb = srgb_to_linear(lb);
        }

        fb[ii]   = (uint8_t)(lr * 63.0); // float to 6bit
        fb[ii+1] = (uint8_t)(lg * 63.0);
        fb[ii+2] = (uint8_t)(lb * 63.0);
    }
}


esp_err_t upload_post_handler(httpd_req_t *req)
{
    char query[128] = {0};
    bool  srgb = true;
    bool  led  = true;
    float redScale = 1.0f;
    float greenScale = 1.0f;
    float blueScale = 1.0f;
    float gamma = 2.4f;

    // --- Parse URL query string ---
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[16];

        if (httpd_query_key_value(query, "srgb", param, sizeof(param)) == ESP_OK)
            srgb = atoi(param);

        if (httpd_query_key_value(query, "led", param, sizeof(param)) == ESP_OK)
            led = atoi(param);

        if (httpd_query_key_value(query, "r", param, sizeof(param)) == ESP_OK)
            redScale = atof(param);

        if (httpd_query_key_value(query, "g", param, sizeof(param)) == ESP_OK)
            greenScale = atof(param);

        if (httpd_query_key_value(query, "b", param, sizeof(param)) == ESP_OK)
            blueScale = atof(param);

        if (httpd_query_key_value(query, "gamma", param, sizeof(param)) == ESP_OK)
            gamma = atof(param);
    }

    ESP_LOGI("HTTP", "srgb=%d led=%d R=%.2f G=%.2f B=%.2f Gamma=%.2f",
             srgb, led, redScale, greenScale, blueScale, gamma);

    int total_len = req->content_len;
    if (total_len != WIDTH * HEIGHT * 3) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid data length");
        return ESP_FAIL;
    }

    int received = 0;
    while (received < total_len) {
        int ret = httpd_req_recv(req,
                                 (char*)framebuffer + received,
                                 total_len - received);
        if (ret <= 0) return ESP_FAIL;
        received += ret;
    }

    // Now framebuffer contains raw RGB.
    // You can apply:
    //   sRGB → linear (gammaIn)
    //   scaling
    //   LED gamma (gammaOut)
    // before sending to HT16D35A.
    process_image_6bit(framebuffer, received, true);

    ht16d35a_load_icon(ht16d35a, framebuffer, received);

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}


httpd_uri_t hello = {
    .uri       = "/hello",
    .method    = HTTP_GET,
    .handler   = hello_get_handler,
    .user_ctx  = NULL
};

httpd_uri_t index_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_get_handler,
    .user_ctx = NULL
};

httpd_uri_t upload_uri = {
    .uri = "/upload",
    .method = HTTP_POST,
    .handler = upload_post_handler,
    .user_ctx = NULL
};

httpd_handle_t server = NULL;
httpd_config_t config = HTTPD_DEFAULT_CONFIG();

void start_webserver() {
    memset(framebuffer, 0, sizeof(framebuffer));
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &hello);
        httpd_register_uri_handler(server, &upload_uri);
    }
}

const char html_page[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>Upload 16x16 LED Icon</title>
<style>
body {
    font-family: Arial, sans-serif;
    margin: 20px;
}
label {
    display: inline-block;
    width: 120px;
}
input {
    margin-bottom: 10px;
}
</style>
</head>

<body>

<h2>Upload 16x16 LED Icon</h2>

<input type="file" id="fileInput" accept="image/*"><br><br>

<div>
<label>srgb:</label>
<input type="checkbox" id="srgb"><br>

<label>led:</label>
<input type="checkbox" id="led"><br>

<label>Red Scale:</label>
<input type="number" id="red" step="0.01" value="1.00"><br>

<label>Green Scale:</label>
<input type="number" id="green" step="0.01" value="1.0"><br>

<label>Blue Scale:</label>
<input type="number" id="blue" step="0.01" value="1.0"><br>

<label>Gamma:</label>
<input type="number" id="gamma" step="0.1" value="2.4"><br>
</div>

<script>

const esp32Url = '/upload';

const canvas = document.createElement('canvas');
canvas.width = 16;
canvas.height = 16;
const ctx = canvas.getContext('2d');

document.getElementById('fileInput').addEventListener('change', async (e) => {

    const file = e.target.files[0];
    if (!file) return;

    const img = new Image();

    img.onload = () => {

        // Resize image to 16x16
        ctx.clearRect(0, 0, 16, 16);
        ctx.drawImage(img, 0, 0, 16, 16);

        const imgData = ctx.getImageData(0, 0, 16, 16).data;
        const rgbData = new Uint8Array(16 * 16 * 3);

        // Extract RGB (ignore alpha)
        for (let i = 0, j = 0; i < imgData.length; i += 4, j += 3) {
            rgbData[j]     = imgData[i];     // R
            rgbData[j + 1] = imgData[i + 1]; // G
            rgbData[j + 2] = imgData[i + 2]; // B
        }

        // Read calibration inputs
        const srgb  = document.getElementById('srgb').checked ? 1 : 0;
        const led   = document.getElementById('led').checked ? 1 : 0;
        const r     = document.getElementById('red').value || 1.0;
        const g     = document.getElementById('green').value || 1.0;
        const b     = document.getElementById('blue').value || 1.0;
        const gamma = document.getElementById('gamma').value || 2.4;

        const url = `/upload?srgb=${srgb}&led=${led}&r=${r}&g=${g}&b=${b}&gamma=${gamma}`;

        fetch(url, {
            method: 'POST',
            headers: { 'Content-Type': 'application/octet-stream' },
            body: rgbData
        })
        .then(resp => resp.text())
        .then(text => {
            console.log('Upload response:', text);
            alert('Upload complete');
        })
        .catch(err => {
            console.error('Upload failed:', err);
            alert('Upload failed');
        });
    };

    img.src = URL.createObjectURL(file);
});

</script>

</body>
</html>
)rawliteral";
