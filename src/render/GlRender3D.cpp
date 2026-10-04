#include "render/GlRender3D.h"

#include <QGuiApplication>
#include <QMatrix4x4>
#include <QOffscreenSurface>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>

#include <cmath>
#include <unordered_map>

namespace Edraw_Desktop {

namespace {

const char* kVertex = R"(
#ifdef GL_ES
precision highp float;
#endif
attribute vec3 pos;
attribute vec3 nrm;
uniform mat4 mvp;
varying vec3 vN;
varying vec3 vW;
void main() {
    vN = nrm;
    vW = pos;
    gl_Position = mvp * vec4(pos, 1.0);
}
)";

const char* kFragment = R"(
#ifdef GL_ES
precision highp float;
#endif
uniform vec4 color;
uniform vec3 light;
uniform vec4 section;
uniform float cut;
uniform float unlit;
uniform float tex;
uniform vec3 eye;
varying vec3 vN;
varying vec3 vW;
float hash3(vec3 p) { return fract(sin(p.x * 12.9898 + p.y * 78.233 + p.z * 45.164) * 43758.5453); }
void main() {
    if (cut > 0.5 && dot(vW, section.xyz) > section.w) discard;
    vec3 n = normalize(vN);
    float lit = unlit > 0.5 ? 1.0 : 0.28 + 0.72 * abs(dot(n, light));
    vec3 rgb = color.rgb * lit;
    if (unlit < 0.5 && tex > 0.5) {
        vec3 toEye = normalize(eye - vW);
        if (dot(n, toEye) < 0.0) n = -n;
        vec3 r = n * (2.0 * dot(n, light)) - light;
        float facing = max(0.0, dot(r, toEye));
        float scale = 1.0;
        float shine = 0.0;
        if (tex < 1.5) {
            scale = 0.93 + 0.05 * hash3(vec3(floor(vW.z * 2.5), floor((vW.x + vW.y) * 0.15), 0.0)) + 0.02 * hash3(floor(vW * 6.0));
            shine = 0.5 * pow(facing, 24.0);
        } else if (tex < 2.5) {
            scale = 0.95 + 0.07 * hash3(floor(vW * 5.0));
            shine = 0.18 * pow(facing, 10.0);
        } else {
            scale = 0.96 + 0.06 * hash3(floor(vW * 3.0));
        }
        rgb = clamp(rgb * scale + vec3(shine), 0.0, 1.0);
    }
    gl_FragColor = vec4(rgb, color.a);
}
)";

// A MESH ON THE CARD: its triangles one after another, each corner with its face's normal - and what it was made
// from, to know it again (or know it changed under the same address).
struct OnCard {
    QOpenGLBuffer buffer{QOpenGLBuffer::VertexBuffer};
    int corners = 0;
    size_t nv = 0, nf = 0;
    double signature = 0.0;
    V3 lo, hi;
    int lastFrame = 0;
};

double signatureOf(const Mesh3D& m) {
    double s = 0.0;
    const size_t n = m.v.size();
    const size_t step = std::max<size_t>(1, n / 16);
    for (size_t i = 0; i < n; i += step) s += m.v[i].x + 2.0 * m.v[i].y + 3.0 * m.v[i].z;
    return s;
}

} // namespace

struct GlRenderer3D::Impl {
    bool tried = false, ok = false;
    QString why;
    std::unique_ptr<QOffscreenSurface> surface;
    std::unique_ptr<QOpenGLContext> context;
    std::unique_ptr<QOpenGLShaderProgram> program;
    std::unique_ptr<QOpenGLFramebufferObject> fbo;
    std::unordered_map<const Mesh3D*, std::unique_ptr<OnCard>> cache;
    int frame = 0;
    using PolygonModeFn = void(QOPENGLF_APIENTRYP)(GLenum, GLenum);
    PolygonModeFn polygonMode = nullptr;

    bool init() {
        tried = true;
        const QString platform = QGuiApplication::platformName();
        if (platform == QLatin1String("offscreen") || platform == QLatin1String("minimal")) {
            why = QStringLiteral("no graphics card under the %1 platform").arg(platform);
            return false;
        }
        context = std::make_unique<QOpenGLContext>();
        QSurfaceFormat format;
        format.setDepthBufferSize(24);
        context->setFormat(format);
        if (!context->create()) {
            why = QStringLiteral("OpenGL could not be started");
            return false;
        }
        surface = std::make_unique<QOffscreenSurface>();
        surface->setFormat(context->format());
        surface->create();
        if (!surface->isValid() || !context->makeCurrent(surface.get())) {
            why = QStringLiteral("OpenGL could not draw off screen");
            return false;
        }
        if (context->format().majorVersion() < 2) {
            why = QStringLiteral("OpenGL %1.%2 is too old").arg(context->format().majorVersion()).arg(context->format().minorVersion());
            context->doneCurrent();
            return false;
        }
        program = std::make_unique<QOpenGLShaderProgram>();
        program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertex);
        program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragment);
        program->bindAttributeLocation("pos", 0);
        program->bindAttributeLocation("nrm", 1);
        if (!program->link()) {
            why = QStringLiteral("OpenGL shaders would not build: %1").arg(program->log().trimmed());
            program.reset();
            context->doneCurrent();
            return false;
        }
        if (!context->isOpenGLES()) polygonMode = reinterpret_cast<PolygonModeFn>(context->getProcAddress("glPolygonMode"));
        context->doneCurrent();
        return true;
    }

    OnCard* upload(const Mesh3D& m) {
        auto& slot = cache[&m];
        const double sig = signatureOf(m);
        if (slot && slot->nv == m.v.size() && slot->nf == m.f.size() && slot->signature == sig) return slot.get();
        if (slot) slot->buffer.destroy();
        slot = std::make_unique<OnCard>();
        slot->nv = m.v.size();
        slot->nf = m.f.size();
        slot->signature = sig;
        m.bounds(slot->lo, slot->hi);
        std::vector<float> data;
        data.reserve(m.f.size() * 18);
        for (const auto& t : m.f) {
            const V3& a = m.v[static_cast<size_t>(t[0])];
            const V3& b = m.v[static_cast<size_t>(t[1])];
            const V3& c = m.v[static_cast<size_t>(t[2])];
            V3 n = cross3(b - a, c - a).normalized();
            if (n.length() < 0.5) n = {0.0, 0.0, 1.0};
            for (const V3* p : {&a, &b, &c}) {
                data.insert(data.end(), {static_cast<float>(p->x), static_cast<float>(p->y), static_cast<float>(p->z),
                                         static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z)});
            }
        }
        slot->corners = static_cast<int>(m.f.size() * 3);
        slot->buffer.create();
        slot->buffer.bind();
        slot->buffer.allocate(data.data(), static_cast<int>(data.size() * sizeof(float)));
        slot->buffer.release();
        return slot.get();
    }

    ~Impl() {
        if (context && surface && context->makeCurrent(surface.get())) {
            for (auto& [mesh, on] : cache) on->buffer.destroy();
            cache.clear();
            program.reset();
            fbo.reset();
            context->doneCurrent();
        }
    }
};

GlRenderer3D::GlRenderer3D() : d_(std::make_unique<Impl>()) {}
GlRenderer3D::~GlRenderer3D() = default;

bool GlRenderer3D::available() {
    if (!d_->tried) d_->ok = d_->init();
    return d_->ok;
}

QString GlRenderer3D::why() const { return d_->why; }

bool GlRenderer3D::render(QImage& image, const std::vector<DrawItem3D>& items, const Camera3D& cam, bool wireframe,
                          const QColor& background, const Section3D& section) {
    if (!available()) return false;
    if (wireframe && !d_->polygonMode) return false;
    const int w = image.width(), h = image.height();
    if (w <= 0 || h <= 0) return false;
    if (!d_->context->makeCurrent(d_->surface.get())) return false;
    QOpenGLFunctions* gl = d_->context->functions();
    if (!d_->fbo || d_->fbo->size() != QSize(w, h)) {
        d_->fbo.reset();
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        format.setSamples(4);
        d_->fbo = std::make_unique<QOpenGLFramebufferObject>(w, h, format);
        if (!d_->fbo->isValid()) {
            format.setSamples(0);
            d_->fbo = std::make_unique<QOpenGLFramebufferObject>(w, h, format);
        }
        if (!d_->fbo->isValid()) {
            d_->fbo.reset();
            d_->context->doneCurrent();
            return false;
        }
    }
    ++d_->frame;
    // EVERY MESH ON THE CARD, and how deep the scene goes - for the far plane.
    V3 right, up, forward;
    cam.axes(right, up, forward);
    const V3 eye = cam.eye();
    std::vector<std::pair<const DrawItem3D*, OnCard*>> drawn;
    double deepest = cam.nearPlane() * 10.0;
    for (const DrawItem3D& it : items) {
        if (!it.mesh || it.mesh->empty() || it.alpha <= 0.001) continue;
        OnCard* on = d_->upload(*it.mesh);
        on->lastFrame = d_->frame;
        drawn.push_back({&it, on});
        for (int i = 0; i < 8; ++i) {
            const V3 c{(i & 1) ? on->hi.x : on->lo.x, (i & 2) ? on->hi.y : on->lo.y, (i & 4) ? on->hi.z : on->lo.z};
            deepest = std::max(deepest, dot3(c - eye, forward));
        }
    }
    // Meshes not drawn for a while are let go - the harness is built again often.
    for (auto it = d_->cache.begin(); it != d_->cache.end();) {
        if (d_->frame - it->second->lastFrame > 120) {
            it->second->buffer.destroy();
            it = d_->cache.erase(it);
        } else {
            ++it;
        }
    }
    d_->fbo->bind();
    gl->glViewport(0, 0, w, h);
    gl->glClearColor(background.redF(), background.greenF(), background.blueF(), 1.0f);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl->glEnable(GL_DEPTH_TEST);
    gl->glDepthFunc(GL_LEQUAL);
    gl->glDisable(GL_CULL_FACE);
    QMatrix4x4 projection, view;
    projection.perspective(static_cast<float>(cam.fovDeg), static_cast<float>(w) / static_cast<float>(h),
                           static_cast<float>(cam.nearPlane()), static_cast<float>(deepest * 1.05 + 1.0));
    view.lookAt(QVector3D(static_cast<float>(eye.x), static_cast<float>(eye.y), static_cast<float>(eye.z)),
                QVector3D(static_cast<float>(cam.target.x), static_cast<float>(cam.target.y), static_cast<float>(cam.target.z)),
                QVector3D(static_cast<float>(up.x), static_cast<float>(up.y), static_cast<float>(up.z)));
    const V3 light = (forward * -1.0 + up * 0.6 + right * -0.35).normalized();
    QOpenGLShaderProgram& p = *d_->program;
    p.bind();
    p.setUniformValue("mvp", projection * view);
    p.setUniformValue("light", QVector3D(static_cast<float>(light.x), static_cast<float>(light.y), static_cast<float>(light.z)));
    p.setUniformValue("section", QVector4D(static_cast<float>(section.normal.x), static_cast<float>(section.normal.y),
                                           static_cast<float>(section.normal.z), static_cast<float>(section.offset)));
    p.setUniformValue("unlit", wireframe ? 1.0f : 0.0f);
    p.setUniformValue("eye", QVector3D(static_cast<float>(eye.x), static_cast<float>(eye.y), static_cast<float>(eye.z)));
    if (wireframe) d_->polygonMode(GL_FRONT_AND_BACK, 0x1B01 /* GL_LINE */);
    p.enableAttributeArray(0);
    p.enableAttributeArray(1);
    // SOLID FIRST, THEN SEE-THROUGH over it, writing no depth so it hides nothing behind it.
    for (int pass = 0; pass < 2; ++pass) {
        const bool see = pass == 1;
        gl->glDepthMask(see ? GL_FALSE : GL_TRUE);
        if (see) {
            gl->glEnable(GL_BLEND);
            gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        } else {
            gl->glDisable(GL_BLEND);
        }
        for (const auto& [it, on] : drawn) {
            if ((it->alpha < 0.999) != see) continue;
            const QColor c = wireframe ? it->color.lighter(130) : it->color;
            p.setUniformValue("color", QVector4D(c.redF(), c.greenF(), c.blueF(), static_cast<float>(it->alpha)));
            p.setUniformValue("cut", it->sectioned && section.on ? 1.0f : 0.0f);
            p.setUniformValue("tex", static_cast<float>(it->texture));
            on->buffer.bind();
            p.setAttributeBuffer(0, GL_FLOAT, 0, 3, static_cast<int>(6 * sizeof(float)));
            p.setAttributeBuffer(1, GL_FLOAT, static_cast<int>(3 * sizeof(float)), 3, static_cast<int>(6 * sizeof(float)));
            gl->glDrawArrays(GL_TRIANGLES, 0, on->corners);
            on->buffer.release();
        }
    }
    gl->glDepthMask(GL_TRUE);
    gl->glDisable(GL_BLEND);
    if (wireframe) d_->polygonMode(GL_FRONT_AND_BACK, 0x1B02 /* GL_FILL */);
    p.disableAttributeArray(0);
    p.disableAttributeArray(1);
    p.release();
    d_->fbo->release();
    image = d_->fbo->toImage(true).convertToFormat(image.format());
    d_->context->doneCurrent();
    return true;
}

} // namespace Edraw_Desktop
