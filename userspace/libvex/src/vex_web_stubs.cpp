#include <AK/FlyString.h>
#include <AK/Random.h>
#include <AK/Utf16String.h>
#include <AK/Utf16FlyString.h>
#include <LibGC/Heap.h>
#include <LibCore/ElapsedTimer.h>
#include <LibCore/AnonymousBuffer.h>
#include <LibGfx/ColorSpace.h>
#include <LibJS/Runtime/AbstractOperations.h>
#include <LibJS/Runtime/Completion.h>
#include <LibJS/Runtime/Error.h>
#include <LibJS/Runtime/ErrorData.h>
#include <LibJS/Runtime/FunctionObject.h>
#include <LibJS/Runtime/IndexedProperties.h>
#include <LibJS/Runtime/PrimitiveString.h>
#include <LibJS/Runtime/PropertyDescriptor.h>
#include <LibJS/Runtime/Realm.h>
#include <LibJS/Runtime/Shape.h>
#include <LibJS/Runtime/Intrinsics.h>
#include <LibJS/Runtime/Symbol.h>
#include <LibJS/Runtime/Value.h>
#include <LibJS/Runtime/VM.h>
#include <LibWeb/CSS/ComputedProperties.h>
#include <LibWeb/CSS/CSSStyleProperties.h>
#include <LibWeb/CSS/Display.h>
#include <LibWeb/CSS/Parser/ComponentValue.h>
#include <LibWeb/CSS/StyleProperty.h>
#include <LibWeb/CSS/StyleScope.h>
#include <LibWeb/CSS/StyleValues/AbstractImageStyleValue.h>
#include <LibWeb/CSS/StyleValues/StyleValue.h>
#include <LibWeb/CSS/StyleValues/StyleValueList.h>
#include <LibWeb/ARIA/ARIAMixin.h>
#include <LibWeb/Animations/Animatable.h>
#include <LibWeb/Bindings/MainThreadVM.h>
#include <LibWeb/CSS/CountersSet.h>
#include <LibWeb/DOM/AbstractElement.h>
#include <LibWeb/DOM/DOMTokenList.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/DOM/Event.h>
#include <LibWeb/DOM/EventDispatcher.h>
#include <LibWeb/DOM/HTMLCollection.h>
#include <LibWeb/DOM/MutationObserver.h>
#include <LibWeb/DOM/DocumentFragment.h>
#include <LibWeb/DOM/MutationType.h>
#include <LibWeb/DOM/NodeIterator.h>
#include <LibWeb/DOM/Range.h>
#include <LibWeb/DOM/ShadowRoot.h>
#include <LibWeb/DOM/SlotRegistry.h>
#include <LibWeb/HTML/CustomElements/CustomElementName.h>
#include <LibWeb/HTML/CustomElements/CustomElementRegistry.h>
#include <LibWeb/HTML/EventLoop/EventLoop.h>
#include <LibWeb/HTML/FormAssociatedElement.h>
#include <LibWeb/HTML/HTMLElement.h>
#include <LibWeb/HTML/HTMLLinkElement.h>
#include <LibWeb/HTML/HTMLMediaElement.h>
#include <LibWeb/HTML/HTMLScriptElement.h>
#include <LibWeb/HTML/HTMLTemplateElement.h>
#include <LibWeb/HTML/HTMLOptionElement.h>
#include <LibWeb/HTML/GlobalEventHandlers.h>
#include <LibWeb/HTML/DocumentState.h>
#include <LibWeb/HTML/EventNames.h>
#include <LibWeb/HTML/Navigable.h>
#include <LibWeb/HTML/Numbers.h>
#include <LibWeb/HTML/SessionHistoryEntry.h>
#include <LibWeb/HTML/Scripting/ExceptionReporter.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/HTML/Scripting/SimilarOriginWindowAgent.h>
#include <LibWeb/HighResolutionTime/TimeOrigin.h>
#include <LibWeb/Layout/Node.h>
#include <LibWeb/SVG/SVGScriptElement.h>
#include <LibWeb/UIEvents/EventNames.h>
#include <LibWeb/WebIDL/DOMException.h>
#include <LibHTTP/Cookie/Cookie.h>
#include <LibURL/Origin.h>
#include <LibURL/URL.h>
#include <AK/StackInfo.h>
#include <LibCore/EventLoop.h>
#include <LibSync/ConditionVariable.h>
#include <LibSync/Mutex.h>
#include <LibThreading/Thread.h>
#include <LibThreading/ThreadPool.h>
#include <LibJS/Runtime/Map.h>
#include <LibGfx/Font/Font.h>
#include <LibGfx/Font/Typeface.h>
#include <LibGfx/Font/FontSupport.h>
#include <LibCrypto/BigInt/SignedBigInteger.h>
#include <LibUnicode/CharacterTypes.h>
#include <LibWeb/HTML/Scripting/TemporaryExecutionContext.h>
#include <LibWeb/HTML/SharedResourceRequest.h>
#include <LibWeb/HTML/Parser/SpeculativeHTMLParser.h>
#include <LibWeb/HTML/HTMLSlotElement.h>
#include <LibWeb/HTML/HTMLMeterElement.h>
#include <LibWeb/HTML/HTMLHeadingElement.h>
#include <LibWeb/HTML/HTMLTextAreaElement.h>
#include <LibWeb/HTML/HTMLFieldSetElement.h>
#include <LibWeb/HTML/HTMLFormElement.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/HTML/CustomElements/CustomStateSet.h>
#include <LibWeb/HTML/HTMLInputElement.h>
#include <LibWeb/HTML/HTMLDetailsElement.h>
#include <LibWeb/HTML/HTMLDialogElement.h>
#include <LibWeb/HTML/HTMLStyleElement.h>
#include <LibWeb/MathML/MathMLElement.h>
#include <LibWeb/MimeSniff/MimeType.h>
#include <LibWeb/MimeSniff/Resource.h>
#include <LibWeb/Platform/EventLoopPlugin.h>
#include <LibWeb/Platform/FontPlugin.h>
#include <LibWeb/Platform/Timer.h>
#include <LibWeb/WebIDL/Promise.h>
#include <LibWeb/CSS/Parser/ErrorReporter.h>
#include <LibWeb/CSS/Parser/ArbitrarySubstitutionFunctions.h>
#include <LibWeb/Fetch/Infrastructure/HTTP/Requests.h>
#include <LibWeb/Fetch/Infrastructure/FetchAlgorithms.h>
#include <LibWeb/Fetch/Infrastructure/FetchController.h>
#include <LibHTTP/HeaderList.h>
#include <LibWeb/Fetch/Fetching/Fetching.h>
#include <LibWeb/Dump.h>
#include <LibWeb/SVG/SVGScriptElement.h>
#include <LibWeb/CSS/DescriptorID.h>
#include <LibWeb/CSS/DescriptorNameAndID.h>
#include <LibWeb/CSS/Descriptor.h>
#include <LibWeb/DOM/DocumentLoadEventDelayer.h>

namespace AK {

StackInfo::StackInfo()
    : m_base(0)
    , m_top(0)
{
}

}

extern "C" void vex_glyph_run_d1() asm("_ZN3Gfx8GlyphRunD1Ev");
extern "C" void vex_glyph_run_d2() asm("_ZN3Gfx8GlyphRunD2Ev");
extern "C" void vex_session_history_entry_d1() asm("_ZN3Web4HTML19SessionHistoryEntryD1Ev");
extern "C" void vex_session_history_entry_d2() asm("_ZN3Web4HTML19SessionHistoryEntryD2Ev");
extern "C" void vex_document_state_d1() asm("_ZN3Web4HTML13DocumentStateD1Ev");
extern "C" void vex_document_state_d2() asm("_ZN3Web4HTML13DocumentStateD2Ev");
extern "C" void vex_gfx_filter_d1() asm("_ZN3Gfx6FilterD1Ev");
extern "C" void vex_gfx_filter_d2() asm("_ZN3Gfx6FilterD2Ev");

extern "C" void vex_glyph_run_d1() { }
extern "C" void vex_glyph_run_d2() { }
extern "C" void vex_session_history_entry_d1() { }
extern "C" void vex_session_history_entry_d2() { }
extern "C" void vex_document_state_d1() { }
extern "C" void vex_document_state_d2() { }
extern "C" void vex_gfx_filter_d1() { }
extern "C" void vex_gfx_filter_d2() { }
extern "C" int _setjmp(void*) { return 0; }
extern "C" void perror(char const*) { }

namespace {

template<typename T>
GC::Ref<T> fake_gc_ref()
{
    alignas(T) static unsigned char storage[sizeof(T)] {};
    return *reinterpret_cast<T*>(storage);
}

}

namespace AK {

u32 get_random_uniform(u32 max_bounds)
{
    if (max_bounds == 0)
        return 0;
    u32 value = 0;
    // arc4random_buf is provided by ladybird_freestanding_stubs.cpp via VEX_SYS_GETRANDOM
    ::arc4random_buf(&value, sizeof(value));
    // Rejection sampling to eliminate modulo bias
    u32 const threshold = (u32)(-(u64)max_bounds % max_bounds);
    for (int i = 0; i < 20 && value < threshold; ++i)
        ::arc4random_buf(&value, sizeof(value));
    return value % max_bounds;
}

u64 get_random_uniform_64(u64 max_bounds)
{
    if (max_bounds == 0)
        return 0;
    u64 value = 0;
    ::arc4random_buf(&value, sizeof(value));
    return value % max_bounds;
}

void fill_with_random(Bytes bytes)
{
    if (bytes.data() && bytes.size() > 0)
        ::arc4random_buf(bytes.data(), bytes.size());
}

}

namespace Core {

AnonymousBufferImpl::~AnonymousBufferImpl()
{
}

// ElapsedTimer is provided by LibCore/ElapsedTimer.cpp — no stubs needed.

}

namespace JS {

VM* VM::s_the = nullptr;

GC_DEFINE_ALLOCATOR(PrimitiveString);
GC_DEFINE_ALLOCATOR(Intrinsics);
GC_DEFINE_ALLOCATOR(Shape);

Shape::Shape(Realm& realm)
    : m_realm(realm)
{
}

Shape::~Shape() = default;

void Shape::finalize()
{
    Base::finalize();
}

void Shape::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_realm);
    visitor.visit(m_prototype);
    visitor.visit(m_prototype_chain_validity);
}

void Intrinsics::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_realm);
    visitor.visit(m_empty_object_shape);
    visitor.visit(m_new_object_shape);
}

GC::Ref<PrimitiveString> PrimitiveString::create(VM& vm, String const& string)
{
    auto primitive = vm.heap().allocate<PrimitiveString>(DeferredKind::None);
    primitive->m_utf8_string = string;
    return primitive;
}

GC::Ref<PrimitiveString> PrimitiveString::create(VM& vm, StringView string)
{
    return create(vm, MUST(String::from_utf8(string)));
}

GC::Ref<PrimitiveString> PrimitiveString::create(VM& vm, FlyString const& string)
{
    return create(vm, string.to_string());
}

GC::Ref<PrimitiveString> PrimitiveString::create(VM& vm, Utf16FlyString const&)
{
    return create(vm, String {});
}

GC::Ref<PrimitiveString> PrimitiveString::create_from_unsigned_integer(VM& vm, u64 value)
{
    return create(vm, String::number(value));
}

void PrimitiveString::finalize()
{
}

PrimitiveString::~PrimitiveString() = default;

String Value::to_string_without_side_effects() const
{
    return {};
}

GC::Ref<Shape> Shape::create_prototype_transition(Object*)
{
    return *this;
}

GC::Ref<Shape> Shape::create_delete_transition(PropertyKey const&)
{
    return *this;
}

GC::Ref<Shape> Shape::create_put_transition(PropertyKey const&, PropertyAttributes)
{
    return *this;
}

GC::Ref<Shape> Shape::create_configure_transition(PropertyKey const&, PropertyAttributes)
{
    return *this;
}

GC::Ref<Shape> Shape::create_dictionary_transition()
{
    return *this;
}

GC::Ref<Shape> Shape::clone_for_prototype()
{
    return *this;
}

void Shape::add_property_without_transition(PropertyKey const&, PropertyAttributes)
{
}

void Shape::set_property_attributes_without_transition(PropertyKey const&, PropertyAttributes)
{
}

void Shape::set_prototype_without_transition(Object*)
{
}

void Shape::remove_property_without_transition(PropertyKey const&, u32)
{
}

Optional<PropertyMetadata> Shape::lookup(PropertyKey const&) const
{
    return {};
}

void Shape::for_each_property_in_insertion_order(Function<void(PropertyKey const&, PropertyMetadata const&)> const&) const
{
}

OrderedHashMap<PropertyKey, PropertyMetadata> const& Shape::property_table() const
{
    static OrderedHashMap<PropertyKey, PropertyMetadata> table;
    return table;
}

bool PropertyDescriptor::is_data_descriptor() const
{
    return value.has_value() || writable.has_value();
}

bool PropertyDescriptor::is_accessor_descriptor() const
{
    return get.has_value() || set.has_value();
}

ErrorData::ErrorData(VM&)
{
}

void ErrorData::visit_edges(Cell::Visitor&)
{
}

bool validate_and_apply_property_descriptor(Object*, PropertyKey const&, bool, PropertyDescriptor&, Optional<PropertyDescriptor> const&)
{
    return true;
}

ThrowCompletionOr<Value> call_impl(VM&, FunctionObject&, Value, ReadonlySpan<Value>)
{
    return js_undefined();
}

Completion throw_completion(Value value)
{
    return Completion { Completion::Type::Throw, value };
}

#define VEX_DEFINE_NATIVE_ERROR_CREATE(ClassName)                                      \
    GC::Ref<ClassName> ClassName::create(Realm&) { return fake_gc_ref<ClassName>(); }  \
    GC::Ref<ClassName> ClassName::create(Realm&, Utf16String) { return fake_gc_ref<ClassName>(); } \
    GC::Ref<ClassName> ClassName::create(Realm&, StringView) { return fake_gc_ref<ClassName>(); }

VEX_DEFINE_NATIVE_ERROR_CREATE(EvalError)
VEX_DEFINE_NATIVE_ERROR_CREATE(RangeError)
VEX_DEFINE_NATIVE_ERROR_CREATE(ReferenceError)
VEX_DEFINE_NATIVE_ERROR_CREATE(TypeError)
VEX_DEFINE_NATIVE_ERROR_CREATE(URIError)

#undef VEX_DEFINE_NATIVE_ERROR_CREATE

Optional<ValueAndAttributes> GenericIndexedPropertyStorage::get(u32) const
{
    return {};
}

bool GenericIndexedPropertyStorage::has_index(u32) const
{
    return false;
}

void GenericIndexedPropertyStorage::remove(u32)
{
}

Utf16String Symbol::descriptive_string() const
{
    return {};
}

}

namespace Web::WebIDL {

GC_DEFINE_ALLOCATOR(DOMException);

GC::Ref<DOMException> DOMException::create(JS::Realm& realm, FlyString name, Utf16String const& message)
{
    return realm.create<DOMException>(realm, move(name), message);
}

GC::Ref<DOMException> DOMException::create(JS::Realm& realm)
{
    return realm.create<DOMException>(realm);
}

GC::Ref<DOMException> DOMException::construct_impl(JS::Realm& realm, Utf16String const& message, FlyString name)
{
    return realm.create<DOMException>(realm, move(name), message);
}

DOMException::DOMException(JS::Realm& realm, FlyString name, Utf16String const& message)
    : PlatformObject(realm)
    , ErrorData(realm.vm())
    , m_name(move(name))
    , m_message(message)
{
}

DOMException::DOMException(JS::Realm& realm)
    : PlatformObject(realm)
    , ErrorData(realm.vm())
{
}

DOMException::~DOMException() = default;

void DOMException::initialize(JS::Realm& realm)
{
    Base::initialize(realm);
}

void DOMException::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    ErrorData::visit_edges(visitor);
}

WebIDL::ExceptionOr<void> DOMException::serialization_steps(HTML::TransferDataEncoder&, bool, HTML::SerializationMemory&)
{
    return {};
}

WebIDL::ExceptionOr<void> DOMException::deserialization_steps(HTML::TransferDataDecoder&, HTML::DeserializationMemory&)
{
    return {};
}

}

namespace Web::DOM {

namespace MutationType {
FlyString attributes = "attributes"_fly_string;
FlyString characterData = "characterData"_fly_string;
FlyString childList = "childList"_fly_string;
}

GC::Ptr<HTML::HTMLSlotElement> ShadowRoot::first_slot_with_name(FlyString const&) const
{
    return nullptr;
}

GC::Ptr<HTML::HTMLSlotElement> SlotRegistry::first_slot_with_name(FlyString const&) const
{
    return nullptr;
}

bool SlotRegistry::is_empty() const
{
    return true;
}

bool EventDispatcher::dispatch(GC::Ref<EventTarget>, Event&, bool)
{
    return true;
}

bool EventDispatcher::dispatch(GC::Ref<EventTarget>, Event&, bool, bool& legacy_output_did_listeners_throw)
{
    legacy_output_did_listeners_throw = false;
    return true;
}

GC_DEFINE_ALLOCATOR(TransientRegisteredObserver);

GC::Ref<TransientRegisteredObserver> TransientRegisteredObserver::create(MutationObserver& observer, Bindings::MutationObserverInit const& options, RegisteredObserver& source)
{
    return observer.heap().allocate<TransientRegisteredObserver>(observer, options, source);
}

TransientRegisteredObserver::TransientRegisteredObserver(MutationObserver& observer, Bindings::MutationObserverInit const& options, RegisteredObserver& source)
    : RegisteredObserver(observer, options)
    , m_source(source)
{
}

TransientRegisteredObserver::~TransientRegisteredObserver() = default;

void TransientRegisteredObserver::visit_edges(Cell::Visitor& visitor)
{
    RegisteredObserver::visit_edges(visitor);
    visitor.visit(m_source);
}

GC_DEFINE_ALLOCATOR(RegisteredObserver);

RegisteredObserver::RegisteredObserver(MutationObserver& observer, Bindings::MutationObserverInit const& options)
    : m_observer(observer)
    , m_options(options)
{
}

RegisteredObserver::~RegisteredObserver() = default;

void RegisteredObserver::visit_edges(Cell::Visitor& visitor)
{
    visitor.visit(m_observer);
}

void ShadowRoot::initialize(JS::Realm& realm)
{
    (void)realm;
}

void DocumentFragment::set_host(Element* host)
{
    m_host = host;
}

GC_DEFINE_ALLOCATOR(DocumentFragment);

DocumentFragment::DocumentFragment(Document& document)
    : ParentNode(document, NodeType::DOCUMENT_FRAGMENT_NODE)
{
}

void DocumentFragment::initialize(JS::Realm& realm)
{
    (void)realm;
}

void DocumentFragment::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_host);
}

HashTable<Range*>& Range::live_ranges()
{
    static HashTable<Range*> ranges;
    return ranges;
}

WebIDL::ExceptionOr<void> Range::set_start(GC::Ref<Node>, WebIDL::UnsignedLong)
{
    return {};
}

WebIDL::ExceptionOr<void> Range::set_end(GC::Ref<Node>, WebIDL::UnsignedLong)
{
    return {};
}

#ifndef VEX_LIBWEB_FULL
bool Document::layout_is_up_to_date() const
{
    return true;
}

void Document::element_with_id_was_added(Badge<Element>, GC::Ref<Element>)
{
}

void Document::element_with_id_was_removed(Badge<Element>, GC::Ref<Element>)
{
}

void Document::element_with_name_was_added(Badge<Element>, GC::Ref<Element>)
{
}

void Document::element_with_name_was_removed(Badge<Element>, GC::Ref<Element>)
{
}

void Document::element_id_changed(Badge<Element>, GC::Ref<Element>, Optional<FlyString>)
{
}

void Document::element_name_changed(Badge<Element>, GC::Ref<Element>)
{
}
#endif

void ShadowRoot::register_slot(HTML::HTMLSlotElement&)
{
}

void ShadowRoot::unregister_slot(HTML::HTMLSlotElement&)
{
}

ElementByIdMap& ShadowRoot::element_by_id() const
{
    if (!m_element_by_id)
        m_element_by_id = make<ElementByIdMap>();
    return *m_element_by_id;
}

GC::Ptr<HTML::CustomElementRegistry> ShadowRoot::custom_element_registry() const
{
    return nullptr;
}

GC_DEFINE_ALLOCATOR(ShadowRoot);

ShadowRoot::ShadowRoot(Document& document, Element& host, Bindings::ShadowRootMode mode)
    : DocumentFragment(document)
    , m_mode(mode)
    , m_style_scope(*this)
{
    set_host(&host);
}

EventTarget* ShadowRoot::get_parent(Event const&)
{
    return host();
}

void ShadowRoot::finalize()
{
}

void ShadowRoot::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
}

void NodeIterator::run_pre_removing_steps(Node&)
{
}

void DOMTokenList::associated_attribute_changed(StringView)
{
}

GC::Ref<Event> Event::create(JS::Realm&, FlyString const&, Bindings::EventInit const&)
{
    return fake_gc_ref<Event>();
}

GC::Ref<HTMLCollection> HTMLCollection::create(ParentNode&, Scope, Function<bool(Element const&)>, Function<bool(Element const&, Element const&)>)
{
    return fake_gc_ref<HTMLCollection>();
}

AbstractElement::AbstractElement(GC::Ref<Element> element, Optional<CSS::PseudoElement> pseudo_element)
    : m_element(element)
    , m_pseudo_element(pseudo_element)
{
}

AbstractElement::AbstractElement(Element const& element, Optional<CSS::PseudoElement> pseudo_element)
    : m_element(const_cast<Element&>(element))
    , m_pseudo_element(pseudo_element)
{
}

void AbstractElement::visit(GC::Cell::Visitor& visitor) const
{
    visitor.visit(m_element);
    visitor.visit(m_inheritance_override);
}

}

namespace Gfx {

namespace Details {
struct ColorSpaceImpl {
};
}

ColorSpace::ColorSpace()
    : m_color_space(make<Details::ColorSpaceImpl>())
{
}

ColorSpace::ColorSpace(ColorSpace const&)
    : ColorSpace()
{
}

ColorSpace::ColorSpace(ColorSpace&&)
    : ColorSpace()
{
}

ColorSpace& ColorSpace::operator=(ColorSpace const&)
{
    return *this;
}

ColorSpace& ColorSpace::operator=(ColorSpace&&)
{
    return *this;
}

ColorSpace::~ColorSpace() = default;

ErrorOr<ColorSpace> ColorSpace::from_cicp(Media::CodingIndependentCodePoints)
{
    return ColorSpace {};
}

ErrorOr<ColorSpace> ColorSpace::load_from_icc_bytes(ReadonlyBytes)
{
    return ColorSpace {};
}

}

namespace Web::CSS {

#ifndef VEX_LIBWEB_FULL
StyleScope::StyleScope(GC::Ref<DOM::Node> node)
    : m_node(node)
{
}

void CountersSet::visit_edges(GC::Cell::Visitor&)
{
}

Display ComputedProperties::display() const
{
    return {};
}

void ComputedProperties::for_each_anchor_name(Function<void(FlyString const&)>) const
{
}

StyleProperty::~StyleProperty() = default;

StyleValueList const& StyleValue::as_value_list() const
{
    return *reinterpret_cast<StyleValueList const*>(this);
}

AbstractImageStyleValue const& StyleValue::as_abstract_image() const
{
    return *reinterpret_cast<AbstractImageStyleValue const*>(this);
}

GC::Ref<CSSStyleProperties> CSSStyleProperties::create_element_inline_style(DOM::AbstractElement, Vector<StyleProperty>, OrderedHashMap<FlyString, StyleProperty>)
{
    return fake_gc_ref<CSSStyleProperties>();
}

void CSSStyleProperties::set_declarations_from_text(StringView)
{
}
#endif

}

namespace Web::ARIA {

ARIAMixin::ARIAMixin() = default;
ARIAMixin::~ARIAMixin() = default;

void ARIAMixin::visit_edges(GC::Cell::Visitor&)
{
}

bool ARIAMixin::has_global_aria_attribute() const
{
    return false;
}

Optional<Role> ARIAMixin::role_or_default() const
{
    return {};
}

void ARIAMixin::set_aria_active_descendant_element(GC::Ptr<DOM::Element>)
{
}

void ARIAMixin::set_aria_controls_elements(Optional<Vector<GC::Weak<DOM::Element>>>)
{
}

void ARIAMixin::set_aria_described_by_elements(Optional<Vector<GC::Weak<DOM::Element>>>)
{
}

void ARIAMixin::set_aria_details_elements(Optional<Vector<GC::Weak<DOM::Element>>>)
{
}

void ARIAMixin::set_aria_error_message_elements(Optional<Vector<GC::Weak<DOM::Element>>>)
{
}

void ARIAMixin::set_aria_flow_to_elements(Optional<Vector<GC::Weak<DOM::Element>>>)
{
}

void ARIAMixin::set_aria_labelled_by_elements(Optional<Vector<GC::Weak<DOM::Element>>>)
{
}

void ARIAMixin::set_aria_owns_elements(Optional<Vector<GC::Weak<DOM::Element>>>)
{
}

}

namespace Web::Animations {

void Animatable::visit_edges(JS::Cell::Visitor&)
{
}

bool Animatable::has_css_defined_animations() const
{
    return false;
}

HashMap<FlyString, GC::Ref<CSS::CSSAnimation>>* Animatable::css_defined_animations(Optional<CSS::PseudoElement>)
{
    return nullptr;
}

Animatable::Impl::~Impl() = default;

}

namespace Web::HTML {

GlobalEventHandlers::~GlobalEventHandlers() = default;

void HTMLScriptElement::execute_script()
{
}

void HTMLScriptElement::prepare_script()
{
}

void HTMLTemplateElement::set_template_contents(GC::Ref<DOM::DocumentFragment>)
{
}

void report_exception(JS::Completion const&, JS::Realm&)
{
}

GC::Ptr<CustomElementRegistry> look_up_a_custom_element_registry(DOM::Node const&)
{
    return nullptr;
}

GC::Ptr<CustomElementDefinition> look_up_a_custom_element_definition(GC::Ptr<CustomElementRegistry>, Optional<FlyString> const&, FlyString const&, Optional<String> const&)
{
    return nullptr;
}

bool is_a_global_custom_element_registry(GC::Ptr<CustomElementRegistry>)
{
    return false;
}

bool is_valid_custom_element_name(String const&)
{
    return false;
}

Optional<i32> parse_integer(StringView)
{
    return {};
}

EventLoop& main_thread_event_loop()
{
    alignas(EventLoop) static unsigned char storage[sizeof(EventLoop)] {};
    return *reinterpret_cast<EventLoop*>(storage);
}

void perform_a_microtask_checkpoint()
{
}

void EventLoop::spin_until(GC::Ref<GC::Function<bool()>>)
{
}

void queue_a_microtask(DOM::Document const*, GC::Ref<GC::Function<void()>>)
{
}

void CustomElementRegistry::append_scoped_document(GC::Ref<DOM::Document>)
{
}

WebIDL::ExceptionOr<void> HTMLOptionElement::maybe_clone_into_selectedcontent()
{
    return {};
}

bool HTMLElement::is_form_associated_custom_element() const
{
    return false;
}

void HTMLMediaElement::set_muted(bool)
{
}

void HTMLLinkElement::load_fallback_favicon_if_needed(GC::Ref<DOM::Document>)
{
}

GC::Ptr<WindowProxy> Navigable::active_window_proxy()
{
    return nullptr;
}

GC::Ptr<DOM::Document> Navigable::container_document() const
{
    return nullptr;
}

GC::Ptr<TraversableNavigable> Navigable::traversable_navigable() const
{
    return nullptr;
}

void FormAssociatedElement::set_form(HTMLFormElement*)
{
}

void FormAssociatedElement::set_parser_inserted(Badge<HTMLParser>)
{
}

JS::Object& relevant_global_object(JS::Object const& object)
{
    return const_cast<JS::Object&>(object);
}

SimilarOriginWindowAgent& relevant_similar_origin_window_agent(JS::Object const&)
{
    alignas(SimilarOriginWindowAgent) static unsigned char storage[sizeof(SimilarOriginWindowAgent)] {};
    return *reinterpret_cast<SimilarOriginWindowAgent*>(storage);
}

}

namespace Web::Layout {

#ifndef VEX_LIBWEB_FULL
CSS::Display Node::display() const
{
    return {};
}
#endif

}

namespace URL {

bool URL::equals(URL const& other, ExcludeFragment) const
{
    return m_data.ptr() == other.m_data.ptr();
}

class URL URL::about(String)
{
    return {};
}

class Origin Origin::create_opaque(Origin::OpaqueData::Type type)
{
    Origin::OpaqueData data {};
    data.type = type;
    return Origin { data };
}

}

namespace HTTP::Cookie {

Optional<String> canonicalize_domain(URL::URL const&)
{
    return {};
}

}

namespace Web::SVG {

void SVGScriptElement::process_the_script_element()
{
}

}

namespace Web::Bindings {

void queue_mutation_observer_microtask()
{
}

void invoke_custom_element_reactions(Vector<GC::Weak<DOM::Element>>&)
{
}

}

namespace Web::HighResolutionTime {

DOMHighResTimeStamp unsafe_shared_current_time()
{
    return 0;
}

DOMHighResTimeStamp relative_high_resolution_time(DOMHighResTimeStamp time, JS::Object const&)
{
    return time;
}

DOMHighResTimeStamp current_high_resolution_time(JS::Object const&)
{
    return 0;
}

}

namespace Web::HTML::EventNames {

FlyString load = "load"_fly_string;
FlyString readystatechange = "readystatechange"_fly_string;

}

namespace Web::UIEvents::EventNames {

FlyString keydown = "keydown"_fly_string;
FlyString mousedown = "mousedown"_fly_string;
FlyString pointerdown = "pointerdown"_fly_string;
FlyString pointerup = "pointerup"_fly_string;
FlyString resize = "resize"_fly_string;

}

// ── JS new stubs ───────────────────────────────────────────────────────────

namespace JS {

String PrimitiveString::utf8_string() const
{
    return m_utf8_string.value_or(String {});
}

size_t PrimitiveString::external_memory_size() const { return 0; }
size_t Shape::external_memory_size() const { return 0; }

bool same_value(Value, Value) { return false; }

ThrowCompletionOr<String> Value::to_string(VM&) const { return String {}; }
ThrowCompletionOr<u32> Value::to_u32(VM&) const { return 0u; }
ThrowCompletionOr<size_t> Value::to_length(VM&) const { return size_t(0); }

void VM::save_execution_context_stack() {}
void VM::clear_execution_context_stack() {}
void VM::restore_execution_context_stack() {}

bool Map::map_has(Value const&) const { return false; }
bool Map::map_remove(Value const&) { return false; }
void Map::map_set(Value const&, Value) {}

}

// ── Core::EventLoop stubs ─────────────────────────────────────────────────

namespace Core {

NonnullRefPtr<WeakEventLoopReference> EventLoop::current_weak()
{
    alignas(WeakEventLoopReference) static unsigned char storage[sizeof(WeakEventLoopReference)] {};
    return *reinterpret_cast<WeakEventLoopReference*>(storage);
}

void EventLoop::deferred_invoke(Function<void()>) {}

StrongEventLoopReference WeakEventLoopReference::take()
{
    return StrongEventLoopReference(*this);
}

StrongEventLoopReference::operator bool() const { return false; }
EventLoop* StrongEventLoopReference::operator->() const { return nullptr; }
StrongEventLoopReference::~StrongEventLoopReference() {}

}

// ── Sync stubs ────────────────────────────────────────────────────────────

namespace Sync {

using VexMutex = MutexBase<PolicyNonRecursive, PolicyIntraprocess>;

template<> VexMutex::MutexBase() {}
template<> VexMutex::~MutexBase() {}
template<> bool VexMutex::try_lock() { return true; }
template<> void VexMutex::lock() {}
template<> void VexMutex::unlock() {}

using VexCV = ConditionVariableBase<VexMutex>;

template<> VexCV::ConditionVariableBase(VexMutex& m) : m_to_wait_on(m) {}
template<> VexCV::~ConditionVariableBase() {}
template<> void VexCV::wait() {}
template<> void VexCV::broadcast() {}

}

// ── Threading stubs ───────────────────────────────────────────────────────

namespace Threading {

Thread::Thread(Function<long()>, StringView) {}
Thread::~Thread() = default;
void Thread::start() {}
void Thread::detach() {}

ThreadPool& ThreadPool::the()
{
    alignas(ThreadPool) static unsigned char storage[sizeof(ThreadPool)] {};
    return *reinterpret_cast<ThreadPool*>(storage);
}

void ThreadPool::submit(Function<void()>) {}

}

// ── Gfx font stubs ───────────────────────────────────────────────────────

namespace Gfx {

Font::~Font() = default;

float Font::pixel_size() const { return m_pixel_size; }

Font const& Font::bold_variant() const { return *this; }

bool font_format_is_supported(FontFormat) { return false; }

ErrorOr<NonnullRefPtr<Typeface>> Typeface::try_load_from_temporary_memory(ReadonlyBytes, u32)
{
    return Error::from_string_literal("not supported");
}

}

// ── Crypto stubs ─────────────────────────────────────────────────────────

namespace Crypto {

unsigned SignedBigInteger::hash() const { return 0; }

}

// ── URL stubs ─────────────────────────────────────────────────────────────

namespace URL {

String URL::serialize(ExcludeFragment) const { return {}; }

}

// ── Unicode stubs ─────────────────────────────────────────────────────────

namespace Unicode {

BidiClass bidirectional_class(u32) { return BidiClass::LeftToRight; }

}

// ── Web::DOM new stubs ────────────────────────────────────────────────────

namespace Web::DOM {

Event::Event(JS::Realm& realm, FlyString const& type, Bindings::EventInit const& init)
    : PlatformObject(realm)
    , m_type(type)
    , m_bubbles(init.bubbles)
    , m_cancelable(init.cancelable)
    , m_composed(init.composed)
    , m_initialized(true)
    , m_time_stamp(0)
{
}

void Event::initialize(JS::Realm& realm)
{
    Base::initialize(realm);
}

void Event::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_target);
    visitor.visit(m_related_target);
    visitor.visit(m_current_target);
}

GC::Ptr<CSS::ComputedProperties const> AbstractElement::computed_properties() const { return nullptr; }

Document& AbstractElement::document() const { return m_element->document(); }

void ShadowRoot::for_each_active_css_style_sheet(Function<void(CSS::CSSStyleSheet&)> const&) const {}

ShadowRoot::PartElementMap const& ShadowRoot::part_element_map() const
{
    static ShadowRoot::PartElementMap empty;
    return empty;
}

}

// ── Web::HTML new stubs ───────────────────────────────────────────────────

namespace Web::HTML {

void EventLoop::perform_a_microtask_checkpoint() {}

bool FormAssociatedElement::enabled() const { return false; }
bool FormAssociatedElement::is_candidate_for_constraint_validation() const { return false; }
bool FormAssociatedElement::satisfies_its_constraints() const { return true; }
bool FormAssociatedElement::is_form_associated_element() const { return true; }
bool FormAssociatedElement::is_listed() const { return false; }
bool FormAssociatedElement::is_submittable() const { return false; }
bool FormAssociatedElement::is_resettable() const { return false; }
bool FormAssociatedElement::suffering_from_being_missing() const { return false; }
bool FormAssociatedElement::suffering_from_a_type_mismatch() const { return false; }
bool FormAssociatedElement::suffering_from_a_pattern_mismatch() const { return false; }
bool FormAssociatedElement::suffering_from_being_too_long() const { return false; }
bool FormAssociatedElement::suffering_from_being_too_short() const { return false; }
bool FormAssociatedElement::suffering_from_an_underflow() const { return false; }
bool FormAssociatedElement::suffering_from_an_overflow() const { return false; }
bool FormAssociatedElement::suffering_from_a_step_mismatch() const { return false; }
bool FormAssociatedElement::suffering_from_bad_input() const { return false; }

Optional<Utf16String> FormAssociatedTextControlElement::selected_text_for_stringifier() const { return {}; }
void FormAssociatedTextControlElement::handle_insert(FlyString const&, Utf16String const&) {}
void FormAssociatedTextControlElement::handle_delete(FlyString const&) {}
void FormAssociatedTextControlElement::select_all() {}
void FormAssociatedTextControlElement::set_selection_anchor(GC::Ref<DOM::Node>, size_t) {}
void FormAssociatedTextControlElement::set_selection_focus(GC::Ref<DOM::Node>, size_t) {}
void FormAssociatedTextControlElement::move_cursor_to_start(CollapseSelection) {}
void FormAssociatedTextControlElement::move_cursor_to_end(CollapseSelection) {}
void FormAssociatedTextControlElement::increment_cursor_position_to_next_word(CollapseSelection) {}
void FormAssociatedTextControlElement::decrement_cursor_position_to_previous_word(CollapseSelection) {}
void FormAssociatedTextControlElement::increment_cursor_position_to_next_line(CollapseSelection) {}
void FormAssociatedTextControlElement::decrement_cursor_position_to_previous_line(CollapseSelection) {}

bool HTMLFieldSetElement::is_disabled() const { return false; }
FormAssociatedElement* HTMLFormElement::default_button() const { return nullptr; }
u32 HTMLHeadingElement::heading_level() const { return 1; }

bool HTMLInputElement::checked_applies() const { return false; }
bool HTMLInputElement::is_allowed_to_be_readonly() const { return false; }
Optional<String> HTMLInputElement::placeholder_value() const { return {}; }
bool HTMLInputElement::required_applies() const { return false; }
bool HTMLInputElement::supports_a_picker() const { return false; }
void HTMLInputElement::initialize(JS::Realm& realm) { Base::initialize(realm); }
void HTMLInputElement::visit_edges(Cell::Visitor& visitor) { Base::visit_edges(visitor); }

bool HTMLMediaElement::blocked() const { return false; }
bool HTMLMediaElement::stalled() const { return false; }

double HTMLMeterElement::value() const
{
    return has_attribute(HTML::AttributeNames::value) ? parse_floating_point_number(get_attribute_value(HTML::AttributeNames::value)).value_or(0.0) : 0.0;
}
double HTMLMeterElement::low() const
{
    return has_attribute(HTML::AttributeNames::low) ? parse_floating_point_number(get_attribute_value(HTML::AttributeNames::low)).value_or(min()) : min();
}
double HTMLMeterElement::high() const
{
    return has_attribute(HTML::AttributeNames::high) ? parse_floating_point_number(get_attribute_value(HTML::AttributeNames::high)).value_or(max()) : max();
}
void HTMLMeterElement::initialize(JS::Realm& realm) { Base::initialize(realm); }
void HTMLMeterElement::visit_edges(Cell::Visitor& visitor) { Base::visit_edges(visitor); }

bool HTMLOptionElement::disabled() const { return false; }
void HTMLOptionElement::initialize(JS::Realm& realm) { Base::initialize(realm); }
void HTMLOptionElement::visit_edges(Cell::Visitor& visitor) { Base::visit_edges(visitor); }

void HTMLDetailsElement::initialize(JS::Realm& realm) { Base::initialize(realm); }
void HTMLDetailsElement::visit_edges(Cell::Visitor& visitor) { Base::visit_edges(visitor); }

void HTMLDialogElement::initialize(JS::Realm& realm) { Base::initialize(realm); }
void HTMLDialogElement::visit_edges(Cell::Visitor& visitor) { Base::visit_edges(visitor); }

void HTMLStyleElement::initialize(JS::Realm& realm) { Base::initialize(realm); }
void HTMLStyleElement::visit_edges(Cell::Visitor& visitor) { Base::visit_edges(visitor); }

Vector<GC::Root<DOM::Node>> HTMLSlotElement::assigned_nodes(Bindings::AssignedNodesOptions) const { return {}; }
Vector<GC::Root<DOM::Element>> HTMLSlotElement::assigned_elements(Bindings::AssignedNodesOptions) const { return {}; }

Optional<String> HTMLTextAreaElement::placeholder_value() const { return {}; }

bool Navigable::has_inclusive_ancestor_with_visibility_hidden() const { return false; }
GC::Ptr<NavigableContainer> Navigable::container() const { return nullptr; }

TaskID queue_a_task(HTML::Task::Source, GC::Ptr<EventLoop>, GC::Ptr<DOM::Document>, GC::Ref<GC::Function<void()>> steps)
{
    steps->function()();
    return {};
}

TaskID queue_global_task(HTML::Task::Source, JS::Object&, GC::Ref<GC::Function<void()>> steps)
{
    steps->function()();
    return {};
}

GC_DEFINE_ALLOCATOR(SharedResourceRequest);

GC::Ref<SharedResourceRequest> SharedResourceRequest::get_or_create(JS::Realm&, GC::Ref<Page>, URL::URL const&)
{
    return fake_gc_ref<SharedResourceRequest>();
}

bool SharedResourceRequest::needs_fetching() const { return false; }
GC::Ptr<DecodedImageData> SharedResourceRequest::image_data() const { return nullptr; }
void SharedResourceRequest::add_callbacks(Function<void()>, Function<void()>) {}
void SharedResourceRequest::fetch_resource(JS::Realm&, GC::Ref<Fetch::Infrastructure::Request>) {}
void SharedResourceRequest::visit_edges(Cell::Visitor& visitor) { Base::visit_edges(visitor); }
void SharedResourceRequest::finalize() { Base::finalize(); }

GC_DEFINE_ALLOCATOR(SpeculativeHTMLParser);

GC::Ref<SpeculativeHTMLParser> SpeculativeHTMLParser::create(JS::Realm&, GC::Ref<DOM::Document>, String, URL::URL)
{
    return fake_gc_ref<SpeculativeHTMLParser>();
}

void SpeculativeHTMLParser::run() {}
void SpeculativeHTMLParser::stop() {}
void SpeculativeHTMLParser::visit_edges(Cell::Visitor& visitor) { Base::visit_edges(visitor); }

TemporaryExecutionContext::TemporaryExecutionContext(JS::Realm& realm, CallbacksEnabled flags)
    : m_settings(principal_realm_settings_object(realm))
    , m_callbacks_enabled(flags)
{}

TemporaryExecutionContext::~TemporaryExecutionContext() {}

EnvironmentSettingsObject& principal_realm_settings_object(JS::Realm& realm)
{
    alignas(EnvironmentSettingsObject) static unsigned char storage[sizeof(EnvironmentSettingsObject)] {};
    (void)realm;
    return *reinterpret_cast<EnvironmentSettingsObject*>(storage);
}

JS::Realm& EnvironmentSettingsObject::realm()
{
    alignas(JS::Realm) static unsigned char storage[sizeof(JS::Realm)] {};
    return *reinterpret_cast<JS::Realm*>(storage);
}

bool url_matches_about_blank(URL::URL const&) { return false; }
bool url_matches_about_srcdoc(URL::URL const&) { return false; }

GC::Ref<DOM::Document const> Window::document() const
{
    alignas(DOM::Document) static unsigned char storage[sizeof(DOM::Document)] {};
    return *reinterpret_cast<DOM::Document*>(storage);
}

bool CustomStateSet::has_state(FlyString const&) const { return false; }

}

// ── Web::MathML stubs ─────────────────────────────────────────────────────

namespace Web::MathML {

void MathMLElement::initialize(JS::Realm& realm) { Base::initialize(realm); }
void MathMLElement::visit_edges(Visitor& visitor) { Base::visit_edges(visitor); }

}

// ── Web::MimeSniff stubs ──────────────────────────────────────────────────

namespace Web::MimeSniff {

MimeType::MimeType(String type, String subtype)
    : m_type(move(type)), m_subtype(move(subtype))
{
    // Compute essence as "type/subtype"
    m_cached_essence = MUST(String::formatted("{}/{}", m_type, m_subtype));
}

MimeType MimeType::create(String type, String subtype)
{
    return MimeType(move(type), move(subtype));
}

MimeType::MimeType(MimeType const& other) = default;
MimeType& MimeType::operator=(MimeType const& other) = default;
MimeType::MimeType(MimeType&&) = default;
MimeType& MimeType::operator=(MimeType&&) = default;
MimeType::~MimeType() = default;
String const& MimeType::essence() const { return m_cached_essence; }
bool MimeType::is_font() const { return false; }
bool MimeType::is_image() const { return false; }
bool MimeType::is_audio_or_video() const { return false; }
bool MimeType::is_zip_based() const { return false; }
bool MimeType::is_archive() const { return false; }
bool MimeType::is_xml() const { return false; }
bool MimeType::is_html() const { return false; }
bool MimeType::is_scriptable() const { return false; }
bool MimeType::is_javascript() const { return false; }
bool MimeType::is_json() const { return false; }
void MimeType::set_parameter(String, String) {}
String MimeType::serialized() const { return m_cached_essence; }
Optional<MimeType> MimeType::parse(StringView) { return {}; }

}

// ── Web::Platform stubs ───────────────────────────────────────────────────

namespace Web::Platform {

static EventLoopPlugin* s_event_loop_plugin = nullptr;
EventLoopPlugin& EventLoopPlugin::the() {
    alignas(EventLoopPlugin) static unsigned char storage[sizeof(EventLoopPlugin)] {};
    if (!s_event_loop_plugin)
        s_event_loop_plugin = reinterpret_cast<EventLoopPlugin*>(storage);
    return *s_event_loop_plugin;
}
void EventLoopPlugin::install(EventLoopPlugin& plugin) { s_event_loop_plugin = &plugin; }
EventLoopPlugin::~EventLoopPlugin() {}
void EventLoopPlugin::spin_until(GC::Root<GC::Function<bool()>>) {}
void EventLoopPlugin::deferred_invoke(GC::Root<GC::Function<void()>> fn) { fn->function()(); }
void EventLoopPlugin::quit() {}

static FontPlugin* s_font_plugin = nullptr;
FontPlugin& FontPlugin::the() {
    alignas(FontPlugin) static unsigned char storage[sizeof(FontPlugin)] {};
    if (!s_font_plugin)
        s_font_plugin = reinterpret_cast<FontPlugin*>(storage);
    return *s_font_plugin;
}
void FontPlugin::install(FontPlugin& plugin) { s_font_plugin = &plugin; }
FontPlugin::~FontPlugin() {}
RefPtr<Gfx::Font> FontPlugin::default_font(float, Optional<Gfx::FontVariationSettings> const&, Optional<Gfx::ShapeFeatures> const&) { return {}; }
Gfx::Font& FontPlugin::default_fixed_width_font()
{
    alignas(Gfx::Font) static unsigned char storage[sizeof(Gfx::Font)] {};
    return *reinterpret_cast<Gfx::Font*>(storage);
}
FlyString FontPlugin::generic_font_name(GenericFont, int, int) { return FlyString {}; }

GC_DEFINE_ALLOCATOR(Timer);

GC::Ref<Timer> Timer::create(GC::Heap&) { return fake_gc_ref<Timer>(); }
GC::Ref<Timer> Timer::create_repeating(GC::Heap&, int, GC::Ptr<GC::Function<void()>>) { return fake_gc_ref<Timer>(); }
GC::Ref<Timer> Timer::create_single_shot(GC::Heap&, int, GC::Ptr<GC::Function<void()>>) { return fake_gc_ref<Timer>(); }
Timer::~Timer() = default;
void Timer::finalize() { Base::finalize(); }
bool Timer::must_survive_garbage_collection() const { return false; }
void Timer::visit_edges(Cell::Visitor& visitor) { Base::visit_edges(visitor); }
void Timer::start() {}
void Timer::stop() {}
void Timer::restart(int) {}
void Timer::set_interval(int) {}
int Timer::interval() const { return 0; }

}

// ── Web::WebIDL new stubs ─────────────────────────────────────────────────

namespace Web::WebIDL {

GC::Ref<Promise> create_promise(JS::Realm&)
{
    return fake_gc_ref<JS::PromiseCapability>();
}

bool is_promise_fulfilled(Promise const&) { return false; }

void resolve_promise(JS::Realm&, Promise const&, JS::Value) {}
void reject_promise(JS::Realm&, Promise const&, JS::Value) {}

}

// ── Remaining Web stubs ───────────────────────────────────────────────────

namespace Web {

JS::Realm& internal_css_realm()
{
    alignas(JS::Realm) static unsigned char storage[sizeof(JS::Realm)] {};
    return *reinterpret_cast<JS::Realm*>(storage);
}

}

namespace Web::Animations {

Bindings::CompositeOperationOrAuto css_animation_composition_to_bindings_composite_operation_or_auto(CSS::AnimationComposition)
{
    return Bindings::CompositeOperationOrAuto::Auto;
}

void KeyframeEffect::generate_initial_and_final_frames(RefPtr<KeyframeEffect::KeyFrameSet>, HashTable<CSS::PropertyID> const&) {}

}

namespace Web::DOMURL {

Optional<URL::URL> parse(StringView, Optional<URL::URL const&>, Optional<StringView>)
{
    return {};
}

}

namespace Web::CSS {

StringView default_stylesheet_source = ""sv;
StringView mathml_stylesheet_source = ""sv;
StringView quirks_mode_stylesheet_source = ""sv;
StringView svg_stylesheet_source = ""sv;

namespace Parser {

ErrorReporter& ErrorReporter::the()
{
    alignas(ErrorReporter) static unsigned char storage[sizeof(ErrorReporter)] {};
    return *reinterpret_cast<ErrorReporter*>(storage);
}

void ErrorReporter::report(Variant<UnknownPropertyError, UnknownRuleError, UnknownMediaFeatureError, UnknownPseudoClassOrElementError, InvalidPropertyError, InvalidValueError, InvalidRuleError, InvalidQueryError, InvalidSelectorError, InvalidPseudoClassOrElementError, InvalidRuleLocationError>&&) {}

Optional<ArbitrarySubstitutionFunction> to_arbitrary_substitution_function(FlyString const&)
{
    return {};
}

Optional<ArbitrarySubstitutionFunctionArguments> parse_according_to_argument_grammar(ArbitrarySubstitutionFunction, Vector<ComponentValue> const&)
{
    return {};
}

}

}

namespace Web::Fetch::Infrastructure {

GC::Ref<Request> Request::create(JS::VM& vm)
{
    return vm.heap().allocate<Request>(HTTP::HeaderList::create());
}

void Request::set_url(URL::URL url)
{
    m_url_list.clear();
    m_url_list.append(move(url));
}

URL::URL& Request::url()
{
    return m_url_list.first();
}

Optional<MimeSniff::MimeType> extract_mime_type(HTTP::HeaderList const&)
{
    return {};
}

GC::Ref<FetchAlgorithms> FetchAlgorithms::create(JS::VM&, FetchAlgorithms::Input)
{
    return fake_gc_ref<FetchAlgorithms>();
}

}

namespace Web::Fetch::Fetching {

GC::Ref<Infrastructure::FetchController> fetch(JS::Realm&, Infrastructure::Request&, Infrastructure::FetchAlgorithms const&, UseParallelQueue)
{
    return fake_gc_ref<Infrastructure::FetchController>();
}

}

namespace Web::HTML {

void Window::visit_edges(Cell::Visitor& visitor) { Base::visit_edges(visitor); }

}

namespace WOFF {
ErrorOr<NonnullRefPtr<Gfx::Typeface>> try_load_from_bytes(ReadonlyBytes, u32)
{
    return Error::from_string_literal("not supported");
}
}

namespace WOFF2 {
ErrorOr<ByteBuffer> convert_to_ttf(ReadonlyBytes)
{
    return Error::from_string_literal("not supported");
}
ErrorOr<NonnullRefPtr<Gfx::Typeface>> try_load_from_bytes(ReadonlyBytes)
{
    return Error::from_string_literal("not supported");
}
}

// ── Core EventLoop stubs (constructor) ────────────────────────────────────

namespace Core {

StrongEventLoopReference::StrongEventLoopReference(WeakEventLoopReference&) {}

}

// ── Gfx::Font::ShapingCache destructor ───────────────────────────────────

namespace Gfx {

Font::ShapingCache::~ShapingCache() {}

}

// ── HTML vtable anchors + missing methods ─────────────────────────────────

namespace Web::HTML {

HTMLDialogElement::~HTMLDialogElement() = default;
void HTMLDialogElement::inserted() {}
void HTMLDialogElement::removed_from(IsSubtreeRoot, Node*, Node&) {}
void HTMLDialogElement::attribute_changed(FlyString const&, Optional<String> const&, Optional<String> const&, Optional<FlyString> const&) {}
bool HTMLDialogElement::is_valid_command(String&) { return false; }
void HTMLDialogElement::command_steps(DOM::Element&, String&) {}

HTMLMeterElement::~HTMLMeterElement() = default;
void HTMLMeterElement::inserted() {}
void HTMLMeterElement::adjust_computed_style(CSS::ComputedProperties&) {}
double HTMLMeterElement::min() const { return 0.0; }
double HTMLMeterElement::max() const { return 1.0; }

HTMLInputElement::~HTMLInputElement() = default;
GC::Ptr<Layout::Node> HTMLInputElement::create_layout_node(GC::Ref<CSS::ComputedProperties>) { return nullptr; }
void HTMLInputElement::adjust_computed_style(CSS::ComputedProperties&) {}
void HTMLInputElement::set_being_activated(bool) {}
Optional<String> HTMLInputElement::optional_value() const { return {}; }
bool HTMLInputElement::is_focusable() const { return false; }
bool HTMLInputElement::is_button() const { return false; }
bool HTMLInputElement::is_submit_button() const { return false; }
void HTMLInputElement::reset_algorithm() {}
void HTMLInputElement::clear_algorithm() {}
void HTMLInputElement::form_associated_element_was_inserted() {}
void HTMLInputElement::form_associated_element_attribute_changed(FlyString const&, Optional<String> const&, Optional<String> const&, Optional<FlyString> const&) {}
WebIDL::ExceptionOr<void> HTMLInputElement::cloned(Node&, bool) const { return {}; }
Optional<ARIA::Role> HTMLInputElement::default_role() const { return {}; }
bool HTMLInputElement::has_activation_behavior() const { return false; }
void HTMLInputElement::activation_behavior(DOM::Event const&) {}
void HTMLInputElement::did_edit_text_node(FlyString const&, Optional<Utf16String> const&) {}
bool HTMLInputElement::suffering_from_being_missing() const { return false; }
bool HTMLInputElement::suffering_from_a_type_mismatch() const { return false; }
bool HTMLInputElement::suffering_from_a_pattern_mismatch() const { return false; }
bool HTMLInputElement::suffering_from_an_underflow() const { return false; }
bool HTMLInputElement::suffering_from_an_overflow() const { return false; }
bool HTMLInputElement::suffering_from_a_step_mismatch() const { return false; }
bool HTMLInputElement::suffering_from_bad_input() const { return false; }
bool HTMLInputElement::is_mutable() const { return false; }
bool HTMLInputElement::uses_button_layout() const { return false; }
void HTMLInputElement::computed_properties_changed() {}
bool HTMLInputElement::is_presentational_hint(FlyString const&) const { return false; }
void HTMLInputElement::apply_presentational_hints(Vector<CSS::StyleProperty>&) const {}

HTMLDetailsElement::~HTMLDetailsElement() = default;
void HTMLDetailsElement::inserted() {}
void HTMLDetailsElement::children_changed(ChildrenChangedMetadata const&) {}
void HTMLDetailsElement::attribute_changed(FlyString const&, Optional<String> const&, Optional<String> const&, Optional<FlyString> const&) {}

HTMLStyleElement::~HTMLStyleElement() = default;
void HTMLStyleElement::children_changed(ChildrenChangedMetadata const&) {}
void HTMLStyleElement::inserted() {}
void HTMLStyleElement::removed_from(IsSubtreeRoot, Node*, Node&) {}
void HTMLStyleElement::attribute_changed(FlyString const&, Optional<String> const&, Optional<String> const&, Optional<FlyString> const&) {}
bool HTMLStyleElement::contributes_a_script_blocking_style_sheet() const { return false; }

HTMLOptionElement::~HTMLOptionElement() = default;
Optional<ARIA::Role> HTMLOptionElement::default_role() const { return {}; }
void HTMLOptionElement::attribute_changed(FlyString const&, Optional<String> const&, Optional<String> const&, Optional<FlyString> const&) {}
void HTMLOptionElement::inserted() {}
void HTMLOptionElement::removed_from(IsSubtreeRoot, Node*, Node&) {}
void HTMLOptionElement::moved_from(IsSubtreeRoot, GC::Ptr<Node>) {}
void HTMLOptionElement::children_changed(ChildrenChangedMetadata const&) {}

}

// ── Web::MathML vtable anchor ─────────────────────────────────────────────

namespace Web::MathML {

MathMLElement::~MathMLElement() = default;
Optional<ARIA::Role> MathMLElement::default_role() const { return {}; }
void MathMLElement::attribute_changed(FlyString const&, Optional<String> const&, Optional<String> const&, Optional<FlyString> const&) {}
WebIDL::ExceptionOr<void> MathMLElement::cloned(DOM::Node&, bool) const { return {}; }
void MathMLElement::inserted() {}
bool MathMLElement::is_presentational_hint(FlyString const&) const { return false; }
void MathMLElement::apply_presentational_hints(Vector<CSS::StyleProperty>&) const {}

}

// ── Web::HTML::parse_floating_point_number ────────────────────────────────

namespace Web::HTML {

Optional<double> parse_floating_point_number(StringView) { return {}; }
Optional<double> parse_floating_point_number(Utf16String const&) { return {}; }

}

// ── CSS parsing stubs ─────────────────────────────────────────────────────

namespace Web {

void dump_rule(StringBuilder&, CSS::CSSRule const&, int) {}
void dump_rule(CSS::CSSRule const&) {}
void dump_descriptors(StringBuilder&, CSS::CSSDescriptors const&, int) {}

GC::Ref<CSS::CSSStyleSheet> parse_css_stylesheet(CSS::Parser::ParsingParams const&, StringView, Optional<URL::URL>, GC::Ptr<CSS::MediaList>)
{
    return fake_gc_ref<CSS::CSSStyleSheet>();
}

RefPtr<CSS::StyleValue const> parse_css_descriptor(CSS::Parser::ParsingParams const&, CSS::AtRuleID, CSS::DescriptorNameAndID const&, StringView)
{
    return {};
}

Vector<CSS::Descriptor> parse_css_descriptor_declaration_block(CSS::Parser::ParsingParams const&, CSS::AtRuleID, StringView)
{
    return {};
}

}

// ── SVGScriptElement stub ─────────────────────────────────────────────────

namespace Web::SVG {

void SVGScriptElement::execute_pending_parser_blocking_script(Badge<HTML::HTMLParser>) {}

}

// ── MimeSniff::Resource::sniff stub ──────────────────────────────────────

namespace Web::MimeSniff {

MimeType Resource::sniff(ReadonlyBytes, SniffingConfiguration)
{
    return MimeType::create(MUST(String::from_utf8("application"sv)), MUST(String::from_utf8("octet-stream"sv)));
}

}

// ── Fetch::Infrastructure stubs ───────────────────────────────────────────

namespace Web::Fetch::Infrastructure {

GC_DEFINE_ALLOCATOR(Request);
Request::Request(NonnullRefPtr<HTTP::HeaderList> header_list)
    : m_header_list(move(header_list))
{}

}

// ── HTTP::HeaderList::create stub ─────────────────────────────────────────

namespace HTTP {

NonnullRefPtr<HeaderList> HeaderList::create(Vector<Header> headers)
{
    return adopt_ref(*new HeaderList(move(headers)));
}

HeaderList::HeaderList(Vector<Header>) {}

}

// ── HTMLElement base class stubs ──────────────────────────────────────────

namespace Web::HTML {

HTMLElement::~HTMLElement() {}
void HTMLElement::initialize(JS::Realm& realm) { Base::initialize(realm); }
void HTMLElement::visit_edges(Cell::Visitor& visitor) { Base::visit_edges(visitor); }
bool HTMLElement::is_focusable() const { return false; }
Optional<ARIA::Role> HTMLElement::default_role() const { return {}; }
void HTMLElement::removed_from(IsSubtreeRoot root, Node* old_ancestor, Node& old_root) { Base::removed_from(root, old_ancestor, old_root); }
void HTMLElement::moved_from(IsSubtreeRoot root, GC::Ptr<DOM::Node> old_ancestor) { Base::moved_from(root, old_ancestor); }
void HTMLElement::attribute_changed(FlyString const& name, Optional<String> const& old_val, Optional<String> const& new_val, Optional<FlyString> const& ns) { Base::attribute_changed(name, old_val, new_val, ns); }
WebIDL::ExceptionOr<void> HTMLElement::cloned(DOM::Node& node, bool clone_children) const { return Base::cloned(node, clone_children); }
void HTMLElement::inserted() { Base::inserted(); }
void HTMLElement::adjust_computed_style(CSS::ComputedProperties&) {}
void HTMLElement::did_receive_focus() {}
void HTMLElement::did_lose_focus() {}

void FormAssociatedElement::reset_algorithm() {}
void FormAssociatedElement::clear_algorithm() {}
void FormAssociatedElement::form_associated_element_was_inserted() {}
void FormAssociatedElement::form_associated_element_was_removed(DOM::Node*) {}
void FormAssociatedElement::form_associated_element_was_moved(GC::Ptr<DOM::Node>) {}
void FormAssociatedElement::form_associated_element_attribute_changed(FlyString const&, Optional<String> const&, Optional<String> const&, Optional<FlyString> const&) {}

Utf16String HTMLInputElement::value() const { return {}; }
WebIDL::ExceptionOr<void> HTMLInputElement::set_value(Utf16String const&) { return {}; }
GC::Ptr<DecodedImageData> HTMLInputElement::image_data() const { return nullptr; }

Utf16String HTMLOptionElement::value() const { return {}; }

}

// ── DocumentLoadEventDelayer stub ─────────────────────────────────────────

namespace Web::DOM {

DocumentLoadEventDelayer::~DocumentLoadEventDelayer() {}

}

// ── Fetch::Infrastructure::Request vtable anchor ─────────────────────────

namespace Web::Fetch::Infrastructure {

void Request::visit_edges(Cell::Visitor& visitor) { Base::visit_edges(visitor); }

}
