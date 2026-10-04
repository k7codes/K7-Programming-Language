# K7 Language

K7, kendi sözdizimi ve çalışma zamanı olan deneysel, dinamik tipli bir programlama dilidir. Yorumlayıcı C++ ile yazılmıştır; `.k7` kaynak dosyaları şu an çalışma anında AST tabanlı yorumlayıcı tarafından yürütülür. K7 kaynak kodu, genel amaçlı bir yerel makine kodu derleyicisi değildir.

Proje; C++/Java'yı andıran süslü parantezli blokları, isteğe bağlı tür bildirimlerini ve Python benzeri kullanışlı koleksiyon/işlev özelliklerini bir araya getirir. Dil hâlen geliştirme aşamasındadır; sözdizimi ve standart kütüphane değişebilir.

## İçindekiler

- [Özellikler](#özellikler)
- [Gereksinimler](#gereksinimler)
- [Derleme](#derleme)
- [İlk program](#ilk-program)
- [Kullanım](#kullanım)
- [Dil özeti](#dil-özeti)
- [Yerleşik işlevler](#yerleşik-işlevler)
- [Standart modüller](#standart-modüller)
- [Proje yapısı](#proje-yapısı)
- [Testler](#testler)
- [Bilinen kapsam ve sınırlamalar](#bilinen-kapsam-ve-sınırlamalar)
- [Katkıda bulunma](#katkıda-bulunma)
- [Lisans](#lisans)

## Özellikler

- Dinamik değer modeli; değişkenler tür bildirimi olmadan da kullanılabilir.
- İsteğe bağlı tür bildirimleri ve tür denetimi: `int`, `long`, `double`, `bool`, `String`, koleksiyonlar ve kullanıcı sınıfları.
- `fn` ile işlev tanımı veya Java/C++ esintili tür-dönüş-değeri sözdizimi.
- `if` / `elif` / `else`, `while`, `for`, `break` ve `continue`.
- Sınıflar, örnek alanları, metotlar ve kalıtım.
- Listeler, tuple'lar, eşlemeler, kümeler, aralıklar ve indeksleme.
- Lambda ifadeleri (`x => x * 2`), `map`, `filter` ve `reduce` gibi işlevsel araçlar.
- String birleştirme, kaçış dizileri, üçlü tırnak ve `$ad` / `${ifade}` biçiminde interpolasyon.
- Hata yakalama (`try` / `catch`) ve `throw`.
- Dosya sistemi, zaman, matematik, JSON, regex, metin, rastgele sayı ve işletim sistemi modülleri.
- Windows için `.k7` dosya ilişkilendirmesi kurma/kaldırma betikleri.
- Belirli türü açıkça belirtilmiş tamsayı toplama döngüleri için dar kapsamlı yorumlayıcı hız yolu.

## Gereksinimler

- Windows 10 veya daha yenisi (mevcut derleme betikleri Windows odaklıdır).
- Visual Studio 2022 veya daha yeni Visual Studio / Build Tools kurulumu.
- Kurulumda **Desktop development with C++** iş yükü ve MSVC x64 araçları.
- GitHub kaynak kopyasında önceden derlenmiş çalıştırılabilir dosya bulunmaz; ilk kullanımda derleme gerekir.

Derleme betiği Visual Studio kurulumunu `vswhere` ile bulmaya çalışır. Visual Studio Installer `vswhere.exe` sağlamıyorsa yaygın VS 2022 kurulum dizinlerini de arar.

## Derleme

Depo klasöründe Komut İstemi veya PowerShell açıp çalıştırın:

```bat
build.bat
```

Başarılı derlemeden sonra yorumlayıcı `build\k7.exe` yolunda oluşur. `build` klasörü üretilen dosyaları içerir ve Git'e eklenmez.

Derleme yardımı:

```bat
build.bat
```

Yorumlayıcının komut satırı seçenekleri:

```bat
build\k7.exe --help
build\k7.exe --version
```

## İlk program

`merhaba.k7` dosyası oluşturun:

```k7
ad = "Dünya"
print("Merhaba $ad!")

fn kare(x) {
    return x * x
}

print("7'nin karesi:", kare(7))
```

Çalıştırın:

```bat
build\k7.exe merhaba.k7
```

Ya da depo klasöründeki kolaylık betiğini kullanın:

```bat
k7.bat merhaba.k7
```

`k7.bat` yorumlayıcıyı depo içindeki `build\k7.exe` dosyasından çağırır; betik başka bir çalışma dizininden çağrılsa da kendi konumunu temel alır.

## Kullanım

### Kaynak dosyası çalıştırma

```bat
build\k7.exe dosya.k7
build\k7.exe dosya.k7 arg1 arg2
```

Program argümanları `argv` listesinde bulunur.

### Tek satır kod

```bat
build\k7.exe -c "print('Merhaba K7')"
```

### Betik sonrasında etkileşimli kabuk

```bat
build\k7.exe -e dosya.k7
```

### Etkileşimli kabuk

Argümansız başlatın:

```bat
build\k7.exe
```

Kabuktan `exit`, `quit` veya `Ctrl+D` ile çıkılır. Windows terminalinde `Ctrl+Z` ardından Enter da EOF gönderebilir.

### `.k7` dosyasına çift tıklama

Önce `build.bat` ile yorumlayıcıyı derleyin. Ardından depo kökündeki `install-association.bat` dosyasını çalıştırarak yalnızca geçerli Windows hesabı için `.k7` ilişkilendirmesi kurabilirsiniz. Komut penceresi program çıktısı görülebilsin diye açık kalır. İlişkilendirmeyi kaldırmak için `uninstall-association.bat` kullanın. Bu betikler makine geneli kayıt defteri ayarlarını değiştirmez; yönetici yetkisi istemez.

## Dil özeti

### Yorumlar ve deyim ayırıcıları

```k7
// Tek satırlık yorum
# Diğer tek satırlık yorum biçimi
/* Çok satırlı
   yorum */

deger = 10
deger += 2; print(deger) // Noktalı virgül de kullanılabilir
```

### Değişkenler ve tür bildirimleri

Tür belirtilmesi isteğe bağlıdır. Bildirilmiş bir tür, değer atanırken ve tipli işlev çağrılırken çalışma anında denetlenir; bu bildirim K7'yi statik derlenen bir dile dönüştürmez.

```k7
ad = "Ada"
var sayac = 0
let etkin = true

int yas = 30
long toplam = 0
double oran = 0.75
bool hazir = false
String mesaj = "K7"
```

### İşlevler

K7 işlevlerinde `fn` biçimi kullanılabilir. Parametre türleri ve `->` ile dönüş türü isteğe bağlıdır:

```k7
fn carp(a, b) {
    return a * b
}

fn topla(int a, int b) -> int {
    return (a + b)
}

int fark(int a, int b) {
    return (a - b)
}

print(carp(6, 7), topla(3, 4), fark(9, 2))
```

Tek satırlı lambda örneği:

```k7
ikiyleCarp = x => x * 2
print(ikiyleCarp(21))
print(map(x => x * 2, [1, 2, 3]))
```

Bu sürümde yalnızca bir üye erişimini, işlev çağrısını veya `match` ifadesini döndürüyorsanız ifadeyi paranteze alın (ör. `return (len(liste))`). Bu, ayrıştırıcının mevcut bir sınırlamasıdır.

### Koşullar ve döngüler

```k7
int puan = 82
if puan >= 90 {
    print("A")
} elif puan >= 70 {
    print("B")
} else {
    print("Tekrar dene")
}

int toplam = 0
for (int i = 1; i <= 5; i++) {
    toplam += i
}
print(toplam) // 15

for eleman in ["a", "b", "c"] {
    print(eleman)
}
```

`i++` ve `i--` artırım/azaltım işlemleri bileşik atamaya dönüştürülür. Klasik `for` başlığındaki bölümler noktalı virgülle ayrılır.

### Sınıflar

```k7
class Sayaç {
    int deger = 0

    void ekle(int miktar) {
        deger += miktar
    }

    int oku() {
        return (deger)
    }
}

Sayaç s = Sayaç()
s.ekle(5)
print(s.oku())
```

Kalıtım `class Alt < Ust { ... }` biçiminde yazılır. Sınıf üyeleri örnek alanları veya işlevlerdir.

### Koleksiyonlar

```k7
sayiListesi = [2, 4, 6]
koordinat = (10, 20)
kisi = {"ad": "Ada", "yas": 30}
benzersiz = #{1, 2, 3}

print(sayiListesi[0])
print(kisi["ad"])
print(2 in benzersiz)
print(len(sayiListesi))
```

### Hata yakalama

```k7
try {
    throw "örnek hata"
} catch hata {
    print("Yakalandı:", hata)
}
```

## Yerleşik işlevler

Çekirdek çalışma zamanı; `print`, `printn`, `echo`, `input`, `len`, `type`, `str`, `repr`, `int`, `float`, `bool`, `list`, `tuple`, `set`, `dict`, `range`, `exit`, `id`, `map`, `filter`, `reduce`, `fold`, `each`, `any`, `all`, `count`, `sum`, `min`, `max`, `sort`, `sorted`, `reverse`, `join`, `split`, `unique`, `flatten`, `zip`, `enumerate`, `take`, `skip`, `keys`, `values`, `items`, `group_by`, `apply`, `error`, `abs`, `round`, `pow` ve `clock` işlevlerini sağlar. İşlev imzalarının ayrıntıları uygulama içinde doğrulanır ve zamanla değişebilir.

## Standart modüller

Modüller `import` ile yüklenir:

```k7
import math
print(math.sqrt(81))

import json
metin = json.stringify({"ad": "Ada"})
print(json.parse(metin)["ad"])
```

| Modül | Kapsam / örnek işlevler |
| --- | --- |
| `math` | Karekök, üs, logaritma, trigonometrik işlevler, yuvarlama, `gcd`, `lcm`, faktöriyel ve aralık sınırlama |
| `time` | `now`, `now_ms`, monotonik saat `mono`, `sleep`, takvim alanları ve zaman biçimlendirme |
| `json` | `parse`, `stringify`, girintili çıktı için `dump` |
| `fs` | Dosya okuma/yazma/ekleme, varlık kontrolleri, boyut, dizin oluşturma, listeleme, yürüyüş, yol yardımcıları ve geçici dizin |
| `random` | Tohumlama, tamsayı/kayan nokta/boolean üretimi, seçim, karıştırma, örnekleme ve normal dağılım |
| `os` | Ortam değişkenleri, argümanlar, CPU sayısı, platform/sürüm, çıkış ve iptal |
| `str` | Büyük/küçük harf, biçimlendirme, birleştirme, yineleme, dolgu, ortalama, kısaltma ve Levenshtein uzaklığı |
| `re` | Regex derleme, eşleşme, arama, tüm eşleşmeler, değiştirme, bölme ve kaçışlama |

Kullanılabilir adlar ve argüman sayıları kaynak kodundaki `src/stdlib/` modül dosyalarında tanımlıdır.

## Proje yapısı

```text
.
├── examples/       # Çalıştırılabilir K7 örnekleri
├── include/k7/     # AST, lexer, parser ve çalışma zamanı başlıkları
├── src/            # Yorumlayıcı ve komut satırı uygulaması
│   └── stdlib/     # Yerleşik modüllerin uygulamaları
├── tests/          # K7 test betikleri ve Windows test çalıştırıcısı
├── tools/          # Geliştirme yardımcı programları
├── build.bat       # Yorumlayıcıyı MSVC ile derler
└── k7.bat          # Derlenmiş yorumlayıcıyı çalıştırır
```

## Testler

Önce projeyi derleyin, sonra test betiğini çalıştırın:

```bat
build.bat
tests\run_tests.bat
```

Test çalıştırıcısı `tests\*.k7` dosyalarını sırayla yürütür, çıkış kodlarını denetler ve başarı/başarısızlık sayısını raporlar. GitHub Actions, Windows üzerinde aynı derleme ve test adımlarını her push ve pull request için çalıştırır. Yeni dil özelliği eklerken mümkünse bağımsız bir `.k7` regresyon dosyası ekleyin.

## Bilinen kapsam ve sınırlamalar

- Proje deneysel ve geliştirme aşamasındadır; geriye dönük uyumluluk garantisi yoktur.
- K7 dinamik bir dildir ve şu an AST yorumlayıcısıyla çalışır. Genel amaçlı AOT/JIT derlemesi veya C++ ile aynı hız garantisi bulunmaz.
- Yorumlayıcı, `long` türleri açıkça yazılmış `for (long i = 0; i < SABİT; i++) { toplam += i; }` biçimindeki basit toplama döngüsünü özel bir hız yoluyla çalıştırır. Bu optimizasyon genel amaçlı derleme değildir ve diğer programların aynı performansı göstereceği anlamına gelmez. `examples/speed_benchmark.k7` bu dar yolu örnekler.
- Tür bildirimleri çalışma zamanı denetimi sağlar; statik analiz/derleme zamanı tür sistemi değildir.
- Ayrıştırıcı bazı doğrudan `return` ifadelerini parantezsiz kabul etmez; tek başına üye erişimi, işlev çağrısı veya `match` dönüşlerini paranteze alın.
- Derleme betikleri Windows ve MSVC'ye göre hazırlanmıştır; Linux/macOS için CMake veya taşınabilir bir build sistemi henüz sağlanmamıştır.
- `.k7` dosya ilişkilendirmesi kullanıcı hesabına kayıt defteri girdileri ekler; bunu kurmak isteğe bağlıdır.
- Test dizini mevcut davranışın bir kısmını kapsar; testlerin geçmesi tüm dil özelliklerinin hatasız olduğunu kanıtlamaz.

Hata bildiriminde işletim sistemi, Visual Studio/MSVC sürümü, çalıştırılan komut ve mümkünse en küçük tekrar üretilebilir `.k7` örneğini ekleyin. Parola, erişim anahtarı veya özel kişisel veri içeren dosyaları paylaşmayın.

## Katkıda bulunma

1. Bir konu/issue açarak değişikliğin kapsamını belirtin.
2. Değişikliği küçük ve odaklı tutun.
3. `build.bat` ile derleyin ve `tests\run_tests.bat` çalıştırın.
4. Yeni davranışı örnek veya test ile belgeleyin.
5. Değişiklik açıklamasında davranış farklarını ve test sonucunu belirtin.

## Lisans

Bu depo MIT Lisansı ile sunulur. Ayrıntılar için [LICENSE](LICENSE) dosyasına bakın.
