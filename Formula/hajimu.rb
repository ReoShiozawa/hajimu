class Hajimu < Formula
  desc "完全日本語プログラミング言語 - はじむ (Hajimu)"
  homepage "https://reoshiozawa.github.io/hajimu-document/"
  url "https://github.com/ReoShiozawa/hajimu/archive/refs/tags/v1.0.0.tar.gz"
  sha256 "" # リリース時に更新
  license "MIT"
  head "https://github.com/ReoShiozawa/hajimu.git", branch: "main"

  depends_on "make" => :build

  def install
    system "make"
    bin.install "nihongo" => "hajimu"
    
    # ドキュメントのインストール
    doc.install "README.md"
    doc.install "docs"
    doc.install "examples"
  end

  test do
    # シンプルなテストプログラム
    (testpath/"test.jp").write <<~EOS
      変数 結果 = 1 + 1
      もし 結果 == 2 なら
        表示("テスト成功")
      終わり
    EOS
    
    assert_match "テスト成功", shell_output("#{bin}/hajimu #{testpath}/test.jp")
  end
end
