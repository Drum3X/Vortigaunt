#include "StudioModelView.h"

#include "core/mdl/StudioModel.h"

#include <QMouseEvent>
#include <QOpenGLShaderProgram>
#include <QPainter>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace {

constexpr int kFloatsPerVertex = 8;  // position, normal, texture coordinates

const char* kVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;
uniform mat4 viewProjection;
out vec3 vNormal;
out vec2 vTexCoord;
void main()
{
    vNormal = normal;
    vTexCoord = texCoord;
    gl_Position = viewProjection * vec4(position, 1.0);
}
)";

const char* kFragmentShader = R"(
#version 330 core
in vec3 vNormal;
in vec2 vTexCoord;
uniform sampler2D tex;
uniform int mode;
uniform vec3 lightDirection;
out vec4 color;
void main()
{
    vec4 texel = texture(tex, vTexCoord);
    if (mode == 1 && texel.a < 0.5)
        discard;
    if (mode == 2)
    {
        color = vec4(texel.rgb, 1.0);
        return;
    }
    float light = 0.55 + 0.6 * max(dot(normalize(vNormal), -lightDirection), 0.0);
    color = vec4(texel.rgb * min(light, 1.2), 1.0);
}
)";

QVector3D toQt(const aiVector3D& v)
{
    return QVector3D(v.x, v.y, v.z);
}

bool sameName(const std::string& a, const std::string& b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

} // namespace

StudioModelView::StudioModelView(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setMinimumSize(200, 200);
    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    m_timer->setInterval(16);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        const float elapsed = m_clock.restart() / 1000.0f;
        if (m_playing)
            m_time += elapsed;
        update();
    });
    m_clock.start();
}

StudioModelView::~StudioModelView()
{
    if (context() && context()->isValid())
    {
        makeCurrent();
        releaseAll();
        delete m_program;
        m_program = nullptr;
        doneCurrent();
    }
}

int StudioModelView::addModel(std::shared_ptr<const StudioModel> model, int mergeInto)
{
    Model entry;
    entry.data = std::move(model);
    entry.mergeInto = mergeInto >= 0 && mergeInto < modelCount() ? mergeInto : -1;
    if (entry.mergeInto >= 0)
    {
        const StudioModel& parent = *m_models[entry.mergeInto].data;
        for (const StudioBone& bone : entry.data->bones)
        {
            int match = -1;
            for (size_t j = 0; j < parent.bones.size() && match < 0; ++j)
            {
                if (sameName(bone.name, parent.bones[j].name))
                    match = static_cast<int>(j);
            }
            entry.mergeBones.push_back(match);
        }
    }
    m_models.push_back(std::move(entry));
    if (m_models.size() == 1)
        m_fitPending = true;
    if (!m_timer->isActive())
        m_timer->start();
    update();
    return modelCount() - 1;
}

void StudioModelView::clearModels()
{
    if (context() && context()->isValid())
    {
        makeCurrent();
        releaseAll();
        doneCurrent();
    }
    m_models.clear();
    m_timer->stop();
    update();
}

std::shared_ptr<const StudioModel> StudioModelView::model(int index) const
{
    return index >= 0 && index < modelCount() ? m_models[index].data : nullptr;
}

void StudioModelView::setSequence(int model, int sequence)
{
    if (model < 0 || model >= modelCount())
        return;
    m_models[model].sequence = sequence;
    m_time = 0.0f;
    update();
}

int StudioModelView::sequence(int model) const
{
    return model >= 0 && model < modelCount() ? m_models[model].sequence : -1;
}

void StudioModelView::setPlaying(bool playing)
{
    m_playing = playing;
    update();
}

void StudioModelView::setBoneOffset(int model, int bone, const aiMatrix4x4& offset)
{
    if (model < 0 || model >= modelCount())
        return;
    m_models[model].offsetBone = bone;
    m_models[model].offset = offset;
    update();
}

aiMatrix4x4 StudioModelView::boneTransform(int model, int bone) const
{
    if (model < 0 || model >= modelCount() || bone < 0 || bone >= static_cast<int>(m_models[model].bones.size()))
        return aiMatrix4x4();
    return m_models[model].bones[bone];
}

void StudioModelView::setGaitSequence(int model, int sequence)
{
    if (model < 0 || model >= modelCount())
        return;
    m_models[model].gaitSequence = sequence;
    update();
}

void StudioModelView::setAimPitch(int model, float degrees)
{
    if (model < 0 || model >= modelCount())
        return;
    m_models[model].aimPitch = degrees;
    update();
}

void StudioModelView::updateBones()
{
    auto playing = [this](const StudioModel& data, int index, float pitch, int& frame, float& blend) {
        frame = 0;
        blend = 0.0f;
        if (index < 0 || index >= static_cast<int>(data.sequences.size()))
            return;
        const StudioSequence& sequence = data.sequences[index];
        frame = static_cast<int>(m_time * std::max(1.0f, sequence.fps)) % std::max(1, sequence.frames);
        if (sequence.blends > 1 && sequence.blendEnd != sequence.blendStart)
            blend = std::clamp((pitch - sequence.blendStart) / (sequence.blendEnd - sequence.blendStart), 0.0f, 1.0f);
    };

    for (Model& model : m_models)
    {
        const StudioModel& data = *model.data;
        int frame = 0;
        float blend = 0.0f;
        playing(data, model.sequence, model.aimPitch, frame, blend);
        std::vector<aiMatrix4x4> local = data.LocalBoneTransforms(model.sequence, frame, blend);

        if (model.mergeInto < 0 && model.gaitSequence >= 0)
        {
            int gaitFrame = 0;
            float gaitBlend = 0.0f;
            playing(data, model.gaitSequence, model.aimPitch, gaitFrame, gaitBlend);
            const std::vector<aiMatrix4x4> gait = data.LocalBoneTransforms(model.gaitSequence, gaitFrame, gaitBlend);
            for (size_t i = 0; i < data.bones.size() && !sameName(data.bones[i].name, "Bip01 Spine"); ++i)
                local[i] = gait[i];
        }

        const Model* parent = model.mergeInto >= 0 ? &m_models[model.mergeInto] : nullptr;
        model.bones.assign(data.bones.size(), aiMatrix4x4());
        std::vector<bool> done(data.bones.size(), false);
        std::vector<int> chain;
        for (size_t i = 0; i < data.bones.size(); ++i)
        {
            chain.clear();
            for (int bone = static_cast<int>(i); bone >= 0 && !done[bone]; bone = data.bones[bone].parent)
                chain.push_back(bone);
            for (size_t k = chain.size(); k-- > 0;)
            {
                const int bone = chain[k];
                const int match = parent ? model.mergeBones[bone] : -1;
                const int up = data.bones[bone].parent;
                if (match >= 0 && match < static_cast<int>(parent->bones.size()))
                    model.bones[bone] = parent->bones[match];
                else
                    model.bones[bone] = up >= 0 ? model.bones[up] * local[bone] : local[bone];
                done[bone] = true;
            }
        }
        if (model.offsetBone >= 0 && model.offsetBone < static_cast<int>(model.bones.size()))
            model.bones[model.offsetBone] = model.bones[model.offsetBone] * model.offset;
    }
}

void StudioModelView::buildVertices(const Model& model, std::vector<float>& out) const
{
    out.clear();
    for (const StudioBodyPart& part : model.data->bodyParts)
    {
        if (part.models.empty())
            continue;
        const StudioSubModel& sub = part.models.front();
        for (const StudioMesh& mesh : sub.meshes)
        {
            for (const StudioTriVertex& corner : mesh.triangles)
            {
                const aiMatrix4x4& bone = model.bones[sub.vertexBones[corner.vertex]];
                const aiVector3D position = bone * sub.vertices[corner.vertex];
                const aiMatrix4x4& normalBone = model.bones[sub.normalBones[corner.normal]];
                const aiVector3D normal = (aiMatrix3x3(normalBone) * sub.normals[corner.normal]).NormalizeSafe();
                out.insert(out.end(), { position.x, position.y, position.z, normal.x, normal.y, normal.z, corner.u, corner.v });
            }
        }
    }
}

void StudioModelView::upload(Model& model)
{
    const StudioModel& data = *model.data;
    model.meshes.clear();
    int first = 0;
    for (const StudioBodyPart& part : data.bodyParts)
    {
        if (part.models.empty())
            continue;
        for (const StudioMesh& mesh : part.models.front().meshes)
        {
            const int count = static_cast<int>(mesh.triangles.size());
            model.meshes.push_back({ mesh.texture, first, count });
            first += count;
        }
    }

    model.textures.assign(data.textures.size(), 0);
    for (size_t i = 0; i < data.textures.size(); ++i)
    {
        const StudioTexture& texture = data.textures[i];
        if (texture.pixels.empty() || texture.palette.size() < 768)
            continue;
        const bool masked = (texture.flags & STUDIO_NF_MASKED) != 0;
        std::vector<uint8_t> rgba(texture.pixels.size() * 4);
        for (size_t p = 0; p < texture.pixels.size(); ++p)
        {
            const uint8_t index = texture.pixels[p];
            rgba[p * 4 + 0] = texture.palette[index * 3 + 0];
            rgba[p * 4 + 1] = texture.palette[index * 3 + 1];
            rgba[p * 4 + 2] = texture.palette[index * 3 + 2];
            rgba[p * 4 + 3] = masked && index == 255 ? 0 : 255;
        }
        glGenTextures(1, &model.textures[i]);
        glBindTexture(GL_TEXTURE_2D, model.textures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texture.width, texture.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, masked ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, masked ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }

    glGenVertexArrays(1, &model.vao);
    glGenBuffers(1, &model.vbo);
    glBindVertexArray(model.vao);
    glBindBuffer(GL_ARRAY_BUFFER, model.vbo);
    const GLsizei stride = kFloatsPerVertex * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(6 * sizeof(float)));
    glBindVertexArray(0);
    model.uploaded = true;
}

void StudioModelView::release(Model& model)
{
    if (!model.uploaded)
        return;
    for (unsigned int texture : model.textures)
    {
        if (texture)
            glDeleteTextures(1, &texture);
    }
    model.textures.clear();
    glDeleteBuffers(1, &model.vbo);
    glDeleteVertexArrays(1, &model.vao);
    model.vbo = model.vao = 0;
    model.uploaded = false;
}

void StudioModelView::releaseAll()
{
    if (!m_glReady)
        return;
    for (Model& model : m_models)
        release(model);
}

void StudioModelView::initializeGL()
{
    if (!initializeOpenGLFunctions())
        return;
    m_program = new QOpenGLShaderProgram();
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader);
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader);
    m_glReady = m_program->link();
}

void StudioModelView::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

bool StudioModelView::bounds(QVector3D& low, QVector3D& high, int onlyModel) const
{
    bool any = false;
    for (int index = 0; index < modelCount(); ++index)
    {
        if (onlyModel >= 0 && index != onlyModel)
            continue;
        const Model& model = m_models[index];
        for (const StudioBodyPart& part : model.data->bodyParts)
        {
            if (part.models.empty())
                continue;
            const StudioSubModel& sub = part.models.front();
            for (size_t v = 0; v < sub.vertices.size(); ++v)
            {
                const QVector3D p = toQt(model.bones[sub.vertexBones[v]] * sub.vertices[v]);
                if (!any)
                {
                    low = high = p;
                    any = true;
                }
                low = QVector3D(std::min(low.x(), p.x()), std::min(low.y(), p.y()), std::min(low.z(), p.z()));
                high = QVector3D(std::max(high.x(), p.x()), std::max(high.y(), p.y()), std::max(high.z(), p.z()));
            }
        }
    }
    return any;
}

void StudioModelView::fitView(int onlyModel)
{
    updateBones();
    QVector3D low, high;
    if (!bounds(low, high, onlyModel))
        return;
    m_target = (low + high) * 0.5f;
    m_distance = std::max(4.0f, (high - low).length() * 1.1f);
    update();
}

void StudioModelView::setCameraAngles(float yaw, float pitch)
{
    m_yaw = yaw;
    m_pitch = std::clamp(pitch, -89.0f, 89.0f);
    update();
}

void StudioModelView::drawModel(Model& model, bool additivePass)
{
    const StudioModel& data = *model.data;
    glBindVertexArray(model.vao);
    for (const Mesh& mesh : model.meshes)
    {
        const int flags = mesh.texture >= 0 ? data.textures[mesh.texture].flags : 0;
        const bool additive = (flags & STUDIO_NF_ADDITIVE) != 0;
        if (additive != additivePass || mesh.count == 0)
            continue;
        const unsigned int texture = mesh.texture >= 0 ? model.textures[mesh.texture] : 0;
        glBindTexture(GL_TEXTURE_2D, texture);
        m_program->setUniformValue("mode", additive ? 2 : ((flags & STUDIO_NF_MASKED) ? 1 : 0));
        glDrawArrays(GL_TRIANGLES, mesh.first, mesh.count);
    }
    glBindVertexArray(0);
}

void StudioModelView::paintGL()
{
    const QColor background = palette().color(QPalette::Base);
    glClearColor(background.redF(), background.greenF(), background.blueF(), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!m_glReady || m_models.empty())
    {
        QPainter painter(this);
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(rect(), Qt::AlignCenter, placeholder);
        return;
    }

    updateBones();
    if (m_fitPending)
    {
        m_fitPending = false;
        QVector3D low, high;
        if (bounds(low, high))
        {
            m_target = (low + high) * 0.5f;
            m_distance = std::max(4.0f, (high - low).length() * 1.1f);
        }
    }

    const float yaw = qDegreesToRadians(m_yaw);
    const float pitch = qDegreesToRadians(m_pitch);
    const QVector3D eye = m_target + m_distance * QVector3D(std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch));
    QMatrix4x4 view;
    view.lookAt(eye, m_target, QVector3D(0, 0, 1));
    QMatrix4x4 projection;
    projection.perspective(45.0f, width() / static_cast<float>(std::max(1, height())), m_distance * 0.01f, m_distance * 50.0f);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CW);
    m_program->bind();
    m_program->setUniformValue("viewProjection", projection * view);
    m_program->setUniformValue("tex", 0);
    m_program->setUniformValue("lightDirection", QVector3D(-0.4f, 0.3f, -1.0f).normalized());
    glActiveTexture(GL_TEXTURE0);

    std::vector<float> vertices;
    for (Model& model : m_models)
    {
        if (!model.uploaded)
            upload(model);
        buildVertices(model, vertices);
        glBindBuffer(GL_ARRAY_BUFFER, model.vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_STREAM_DRAW);
    }

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    for (Model& model : m_models)
        drawModel(model, false);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    for (Model& model : m_models)
        drawModel(model, true);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    m_program->release();
}

void StudioModelView::mousePressEvent(QMouseEvent* event)
{
    m_lastMouse = event->pos();
    m_dragButton = event->button();
}

void StudioModelView::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint delta = event->pos() - m_lastMouse;
    m_lastMouse = event->pos();
    if (m_dragButton == Qt::LeftButton)
    {
        m_yaw -= delta.x() * 0.5f;
        m_pitch = std::clamp(m_pitch + delta.y() * 0.5f, -89.0f, 89.0f);
    }
    else if (m_dragButton == Qt::RightButton || m_dragButton == Qt::MiddleButton)
    {
        const float yaw = qDegreesToRadians(m_yaw);
        const float pitch = qDegreesToRadians(m_pitch);
        const QVector3D forward(-std::cos(pitch) * std::cos(yaw), -std::cos(pitch) * std::sin(yaw), -std::sin(pitch));
        const QVector3D right = QVector3D::crossProduct(forward, QVector3D(0, 0, 1)).normalized();
        const QVector3D up = QVector3D::crossProduct(right, forward).normalized();
        const float scale = m_distance / std::max(1, height());
        m_target += (-right * delta.x() + up * delta.y()) * scale;
    }
    update();
}

void StudioModelView::mouseReleaseEvent(QMouseEvent*)
{
    m_dragButton = Qt::NoButton;
}

void StudioModelView::mouseDoubleClickEvent(QMouseEvent*)
{
    fitView();
}

void StudioModelView::wheelEvent(QWheelEvent* event)
{
    m_distance = std::clamp(m_distance * (event->angleDelta().y() > 0 ? 0.85f : 1.18f), 1.0f, 20000.0f);
    update();
}
